"""Hamster_RPG 서버 부하 테스트 — 봇을 계속 밀어넣으면서 한계를 눈으로 본다.

봇을 0.1초에 한 마리씩 넣다가, 핑이 기준선보다 --ping-rise(기본 10ms) 넘게 오르면
투입을 1초에 한 마리로 늦추고 거기서부터 한 마리씩 넣었다 뺐다 하며 한계선 위를 진동한다.
그 진동의 중앙값이 곧 이 서버가 감당하는 인원이다.

같이 뜨는 창에 월드가 그려진다. 하얀 점이 플레이어(봇), 빨간 점이 몬스터.

준비:
    서버를 랜덤 스폰 모드로 띄운다. 안 그러면 ProcessLogin이 전원을 원점에 스폰시켜서
    봇이 죄다 겹치고, 서로가 서로의 시야에 들어와 AOI가 일할 여지가 없어진다.

        Server\\x64\\Debug\\Server.exe --random-spawn 25

사용법:
    python Tools/loadtest.py --area 25              # 서버에 준 --random-spawn과 같은 값
    python Tools/loadtest.py --area 25 --no-viz     # 창 없이 (측정만)
    python Tools/loadtest.py --area 25 --ping-rise 20

    --area는 밀도를 정한다. 봇당 시야 인원 ≈ 인원 x 1257 / (2*area)^2 니까,
    넓게 주면 AOI가 너무 잘 들어서 서버가 놀고(±150이면 봇당 1명) 한계가 안 나온다.
    ±25가 봇당 40명쯤으로 적당하다.

읽는 법:
    핑      봇을 잠깐 멈췄다 출발시켜 첫 이동 패킷이 돌아오기까지. 틱 대기(0~67ms)가
            원래 섞여 있어서 한가해도 40ms 안팎이 정상이다. 그래서 절대값이 아니라
            기준선 대비 얼마나 올랐는지로 판단한다.
    틱      내 이동 패킷이 오는 간격. 15Hz면 66.7ms가 정상. 서버가 밀리면 늘어진다.
    코어    서버가 쓰는 CPU를 코어 개수로 환산한 값. 이 서버는 게임 틱이 스레드 하나라
            1.0에 붙으면 거기가 천장이다(16코어 머신이면 작업관리자엔 6%로 보인다).
    cli     이 스크립트 자신이 밀린 정도. 25ms를 넘으면 핑이 서버가 아니라 파이썬을
            재고 있는 것이므로 결과를 믿으면 안 된다.
"""

import argparse
import asyncio
import math
import multiprocessing
import os
import random
import struct
import sys
import time
from collections import deque
from struct import unpack_from

try:
    sys.stdout.reconfigure(encoding='utf-8')
except Exception:
    pass

# ---------------- 프로토콜 (protocol.h와 1:1, #pragma pack(1)) ----------------
CS_LOGIN, CS_INPUT = 1, 2
SC_LOGIN_RESULT, SC_SPAWN, SC_DESPAWN, SC_MOVEOBJECT = 5, 6, 7, 8
LOGIN_SIZE, INPUT_SIZE = 24, 12
MONSTER_ID_BASE = 1000000        # GameServer::MONSTER_ID_BASE

TICK_HZ = 15.0
IDEAL_TICK_MS = 1000.0 / TICK_HZ

PROBE_PERIOD = 1.2               # 이 주기마다 잠깐 멈췄다 출발해서 핑을 잰다
PROBE_PAUSE = 0.15
PING_WINDOW = 3.0                # 최근 이만큼의 핑으로 판단한다. 좁으면 노이즈에 휘둘린다.
CLIENT_SATURATED_MS = 25.0       # 이벤트 루프가 이만큼 밀리면 측정이 오염된 것
OVER_STREAK = 3                  # 이만큼 연속 초과해야 진짜 초과로 본다(0.1초 틱 x 3)


def pkt_login(name):
    return struct.pack('<HH20s', LOGIN_SIZE, CS_LOGIN, name.encode()[:20])


def pkt_input(dx, dy):
    return struct.pack('<HHff', INPUT_SIZE, CS_INPUT, dx, dy)


def pct(vals, p):
    if not vals:
        return 0.0
    s = sorted(vals)
    k = min(len(s) - 1, int(round(p / 100.0 * (len(s) - 1))))
    return s[k]


class Stats:
    def __init__(self):
        self.rx_packets = 0
        self.rx_bytes = 0
        self.tx_packets = 0
        self.connect_fail = 0
        self.pings = deque()        # (t, ms) 최근 것만 남긴다
        self.all_pings = []
        self.ticks = deque(maxlen=4000)
        self.loop_lag = deque(maxlen=400)
        self.monsters = {}          # id -> (x, y). 어느 봇이든 본 몬스터의 최신 위치

    def recent_pings(self, now, window=PING_WINDOW):
        while self.pings and self.pings[0][0] < now - window:
            self.pings.popleft()
        return [v for _, v in self.pings]


async def loop_lag_monitor(stats):
    """이 스크립트 자신이 밀리고 있는지 잰다.

    핑에는 서버 처리 시간과 '파이썬이 패킷을 늦게 집어든 시간'이 같이 들어간다. 봇이 많아지면
    후자가 커지는데 그걸 서버 탓으로 오해하면 한계를 실제보다 낮게 잡는다.
    sleep이 제때 안 깨는 정도가 그 오염의 직접 증거다.

    주의: 여기서 나오는 raw 값에는 Windows 타이머 해상도(약 15.6ms)가 상수로 얹혀 있다.
    한가할 때도 14ms쯤 찍히므로, 쓰는 쪽에서 기준선을 빼고 봐야 한다(Runner.lag_base).
    """
    interval = 0.05
    while True:
        t = time.monotonic()
        await asyncio.sleep(interval)
        stats.loop_lag.append((time.monotonic() - t - interval) * 1000.0)


class Bot:
    __slots__ = ('index', 'area', 'stats', 'id', 'x', 'y', 'view', 'dir',
                 'writer', 'last_move_at', 'probe_sent_at', 'alive')

    def __init__(self, index, area, stats):
        self.index = index
        self.area = area
        self.stats = stats
        self.id = None
        self.x = 0.0
        self.y = 0.0
        self.view = 0
        self.dir = (0.0, 0.0)
        self.writer = None
        self.last_move_at = None
        self.probe_sent_at = None
        self.alive = False

    def pick_dir(self):
        # 스폰 영역을 벗어나면 안쪽으로. 봇이 area 안에 머물러야 밀도가 유지된다.
        if self.area > 0 and max(abs(self.x), abs(self.y)) > self.area:
            ang = math.atan2(-self.y, -self.x) + random.uniform(-0.4, 0.4)
        else:
            ang = random.uniform(0.0, 2.0 * math.pi)
        return (math.cos(ang), math.sin(ang))

    def send_input(self, dx, dy):
        self.dir = (dx, dy)
        self.writer.write(pkt_input(dx, dy))
        self.stats.tx_packets += 1

    def handle(self, buf, off, ptype, now):
        st = self.stats
        if ptype == SC_MOVEOBJECT:
            eid = unpack_from('<I', buf, off + 4)[0]
            if eid == self.id:
                self.x, self.y = unpack_from('<ff', buf, off + 8)
                if self.probe_sent_at is not None:
                    # 멈췄다 출발한 뒤 처음 온 내 이동 = 그 입력이 반영된 순간
                    ms = (now - self.probe_sent_at) * 1000.0
                    st.pings.append((now, ms))
                    st.all_pings.append(ms)
                    self.probe_sent_at = None
                elif self.last_move_at is not None:
                    st.ticks.append((now - self.last_move_at) * 1000.0)
                self.last_move_at = now
            elif eid >= MONSTER_ID_BASE:
                st.monsters[eid] = unpack_from('<ff', buf, off + 8)
        elif ptype == SC_SPAWN:
            eid = unpack_from('<I', buf, off + 4)[0]
            if eid != self.id:
                self.view += 1
                if eid >= MONSTER_ID_BASE:
                    st.monsters[eid] = unpack_from('<ff', buf, off + 28)
        elif ptype == SC_DESPAWN:
            self.view -= 1
        elif ptype == SC_LOGIN_RESULT:
            _ok, pid, x, y = unpack_from('<BIff', buf, off + 4)
            self.id = pid
            self.x, self.y = x, y

    async def reader_loop(self, reader):
        buf = bytearray()
        st = self.stats
        while self.alive:
            try:
                chunk = await reader.read(65536)
            except (ConnectionResetError, ConnectionAbortedError, OSError):
                break
            if not chunk:
                break
            buf += chunk
            st.rx_bytes += len(chunk)
            now = time.monotonic()

            off, n = 0, len(buf)
            while n - off >= 4:
                size, ptype = unpack_from('<HH', buf, off)
                if size < 4 or n - off < size:
                    break
                self.handle(buf, off, ptype, now)
                st.rx_packets += 1
                off += size
            if off:
                del buf[:off]

    async def behave_loop(self):
        self.send_input(*self.pick_dir())
        next_probe = time.monotonic() + PROBE_PERIOD * random.uniform(0.2, 1.0)
        next_turn = time.monotonic() + random.uniform(2.0, 5.0)

        while self.alive:
            await asyncio.sleep(0.1)
            now = time.monotonic()

            if now >= next_probe:
                # 정지 중엔 이동 패킷이 안 오므로, 출발 직후 첫 패킷까지가 곧 핑이다.
                saved = self.dir
                self.send_input(0.0, 0.0)
                self.last_move_at = None
                await asyncio.sleep(PROBE_PAUSE)
                if not self.alive:
                    break
                self.probe_sent_at = time.monotonic()
                self.send_input(*saved)
                next_probe = time.monotonic() + PROBE_PERIOD * random.uniform(0.8, 1.2)
                continue

            if now >= next_turn:
                self.send_input(*self.pick_dir())
                next_turn = now + random.uniform(2.0, 5.0)

    async def run(self, host, port):
        try:
            reader, writer = await asyncio.open_connection(host, port)
        except Exception:
            self.stats.connect_fail += 1
            return
        self.writer = writer
        self.alive = True
        try:
            writer.write(pkt_login(f'bot{self.index}'))
            self.stats.tx_packets += 1
            await asyncio.gather(self.reader_loop(reader), self.behave_loop())
        except Exception:
            pass
        finally:
            self.alive = False
            try:
                writer.close()
            except Exception:
                pass


# ---------------- 서버 프로세스 샘플링 (psutil 없이) ----------------
async def server_sample(name):
    ps = (f"$p=Get-Process -Name {name} -ErrorAction SilentlyContinue;"
          f" if($p){{ \"$($p.CPU) $($p.WorkingSet64)\" }}")
    try:
        proc = await asyncio.create_subprocess_exec(
            'powershell', '-NoProfile', '-NonInteractive', '-Command', ps,
            stdout=asyncio.subprocess.PIPE, stderr=asyncio.subprocess.DEVNULL)
        out, _ = await proc.communicate()
        parts = out.decode(errors='ignore').split()
        if len(parts) >= 2:
            return float(parts[0]), int(parts[1])
    except Exception:
        pass
    return None


class Runner:
    FAST, FINE = 'FAST', 'FINE'

    def __init__(self, args, stats, vq):
        self.a = args
        self.st = stats
        self.vq = vq
        self.bots = []
        self.next_index = 0
        self.mode = self.FAST
        self.baseline = None
        self.fine_counts = []        # FINE 모드에서 진동한 봇 수들. 이것의 중앙값이 답.
        self.fine_cores = []         # 그때 서버가 실제로 쓴 코어. 낮으면 가짜 한계다.
        self.over_streak = 0
        self.lag_base = 0.0          # 한가할 때의 루프 밀림(= Windows 타이머 해상도). 이걸 빼야 진짜가 보인다.
        self.peak = 0
        self.trip_point = None       # 처음 기준선을 넘긴 지점 (인원, 핑, 코어)
        self.client_saturated = False
        self.cores = max(1, os.cpu_count() or 1)
        self.cpu_prev = None
        self.cores_used = None
        self.rss = 0
        self.rx_prev = (0, 0, 0.0)
        self.rate = (0.0, 0.0)

    def live(self):
        return sum(1 for b in self.bots if b.alive)

    def add_one(self):
        if len(self.bots) >= self.a.max_bots:
            return
        b = Bot(self.next_index, self.a.area, self.st)
        self.next_index += 1
        self.bots.append(b)
        asyncio.create_task(b.run(self.a.host, self.a.port))

    def remove_one(self):
        alive = [b for b in self.bots if b.alive]
        if len(alive) <= 1:
            return
        b = alive[-1]
        b.alive = False
        try:
            b.writer.close()
        except Exception:
            pass
        self.bots = [x for x in self.bots if x.alive]

    async def sample_server(self):
        s = await server_sample(self.a.proc)
        if not s:
            return
        cpu_total, rss = s
        now = time.monotonic()
        if self.cpu_prev:
            self.cores_used = (cpu_total - self.cpu_prev[0]) / max(1e-6, now - self.cpu_prev[1])
        self.cpu_prev = (cpu_total, now)
        self.rss = rss

    def update_rate(self, now):
        if self.rx_prev[2]:
            dt = now - self.rx_prev[2]
            if dt >= 0.5:
                self.rate = ((self.st.rx_packets - self.rx_prev[0]) / dt,
                             (self.st.rx_bytes - self.rx_prev[1]) / dt / 1048576.0)
                self.rx_prev = (self.st.rx_packets, self.st.rx_bytes, now)
        else:
            self.rx_prev = (self.st.rx_packets, self.st.rx_bytes, now)

    def push_viz(self, p50, over):
        if not self.vq:
            return
        live = [b for b in self.bots if b.alive and b.id is not None]
        base = self.baseline if self.baseline is not None else 0.0
        view = sum(b.view for b in live) / len(live) if live else 0.0
        cores = f'{self.cores_used:.2f}코어' if self.cores_used is not None else '-'
        info = (f'봇 {len(live):4d}    핑 p50 {p50:6.1f}ms   기준선 {base:5.1f} +{self.a.ping_rise:.0f} '
                f'= {base + self.a.ping_rise:5.1f}{"  [초과]" if over else ""}\n'
                f'모드 {self.mode}{"(0.1초/마리)" if self.mode == self.FAST else f"({self.a.fine_interval:.0f}초/마리)"}'
                f'    수신 {self.rate[0]:,.0f} pkt/s  {self.rate[1]:.2f} MB/s   '
                f'봇당 시야 {view:.1f}명   서버 {cores}')
        try:
            self.vq.put_nowait({
                'players': [(b.x, b.y) for b in live],
                'monsters': list(self.st.monsters.values()),
                'info': info,
            })
        except Exception:
            pass   # 뷰어가 밀리면 이번 프레임은 그냥 버린다

    async def run(self):
        a = self.a
        print(f'\n대상 {a.host}:{a.port} | area ±{a.area:.0f} | 기준선 대비 +{a.ping_rise:.0f}ms를 한계로 본다')

        # 1) 기준선 측정: 소수만 붙여 한가할 때의 핑을 잡는다. 이게 있어야 '올랐다'를 말할 수 있다.
        #    동시에 '가만히 있어도 p50이 얼마나 흔들리는지'(노이즈)도 같이 잰다.
        #    핑에는 틱 대기(0~67ms 랜덤)가 늘 섞여 있어서, 부하와 무관하게 p50이 출렁인다.
        #    그 출렁임보다 작은 --ping-rise를 쓰면 서버가 놀고 있어도 한계로 오판하게 된다.
        for _ in range(a.warmup_bots):
            self.add_one()
            await asyncio.sleep(0.05)
        print(f'기준선 측정중... ({a.warmup_bots}마리로 {a.warmup:.0f}초)')

        await asyncio.sleep(PING_WINDOW)
        probes = []
        t_warm = time.monotonic() + max(1.0, a.warmup - PING_WINDOW)
        while time.monotonic() < t_warm:
            await asyncio.sleep(0.25)
            p = pct(self.st.recent_pings(time.monotonic()), 50)
            if p > 0:
                probes.append(p)

        if not probes:
            print('! 핑 샘플이 없다. 서버가 떠 있는지 확인해라.')
            return

        self.baseline = pct(probes, 50)
        self.lag_base = pct(list(self.st.loop_lag), 50)
        noise = (max(probes) - min(probes)) / 2.0
        print(f'기준선 {self.baseline:.1f}ms (한가한데도 ±{noise:.1f}ms 흔들림) '
              f'→ 한계선 {self.baseline + a.ping_rise:.1f}ms')
        if a.ping_rise < noise * 1.5:
            rec = max(10, int(noise * 3 / 5) * 5)
            print(f'! --ping-rise {a.ping_rise:.0f}은 저 흔들림(±{noise:.1f})보다 작다. 서버가 멀쩡해도')
            print(f'  임계를 넘나들며 가짜 한계를 잡는다. --ping-rise {rec} 이상을 권한다.')
            print(f'  (결과의 "서버 N코어"를 꼭 같이 봐라. 1.0에 한참 못 미치면 진짜 한계가 아니다)')
        print()
        print(f"{'t':>4} {'봇':>5} {'핑p50':>7} {'p95':>6} {'틱':>6} {'pkt/s':>8} {'MB/s':>6} "
              f"{'시야':>5} {'코어':>5} {'cli':>4}  상태")
        print('-' * 92)

        end = time.monotonic() + a.duration
        t0 = time.monotonic()
        next_fine = 0.0
        next_log = t0 + 1.0
        next_srv = t0
        next_viz = t0

        while time.monotonic() < end:
            await asyncio.sleep(0.1)
            now = time.monotonic()

            pings = self.st.recent_pings(now)
            p50 = pct(pings, 50)
            over = bool(pings) and p50 > self.baseline + a.ping_rise
            # 한 번 튄 걸로 판단하지 않는다. 연속으로 넘겨야 진짜 초과.
            self.over_streak = self.over_streak + 1 if over else 0
            really_over = self.over_streak >= OVER_STREAK

            cli95 = max(0.0, pct(list(self.st.loop_lag), 95) - self.lag_base)
            if cli95 > CLIENT_SATURATED_MS:
                self.client_saturated = True

            if now >= next_srv:
                await self.sample_server()
                next_srv = now + 2.0
            self.update_rate(now)

            # --- 인원 조절 ---
            if self.mode == self.FAST:
                if really_over:
                    self.mode = self.FINE
                    self.trip_point = (self.live(), p50, self.cores_used)
                    next_fine = now + a.fine_interval
                    print(f'>> {self.live()}명에서 핑이 {p50:.1f}ms로 기준선을 넘었다. '
                          f'여기서부터 {a.fine_interval:.0f}초에 한 마리씩 조절한다.')
                else:
                    self.add_one()          # 0.1초 틱마다 한 마리
            else:
                if now >= next_fine:
                    if over:
                        self.remove_one()
                    else:
                        self.add_one()
                    next_fine = now + a.fine_interval
                    self.fine_counts.append(self.live())
                    if self.cores_used is not None:
                        self.fine_cores.append(self.cores_used)

            live = self.live()
            self.peak = max(self.peak, live)

            if now >= next_viz:
                self.push_viz(p50, over)
                next_viz = now + 0.1

            if now >= next_log:
                next_log = now + 1.0
                view = (sum(b.view for b in self.bots if b.alive) / live) if live else 0.0
                cores_s = f'{self.cores_used:5.2f}' if self.cores_used is not None else '    -'
                tick50 = pct(list(self.st.ticks), 50)
                print(f'{now - t0:4.0f} {live:5d} {p50:7.1f} {pct(pings, 95):6.1f} {tick50:6.1f} '
                      f'{self.rate[0]:8.0f} {self.rate[1]:6.2f} {view:5.1f} {cores_s} {cli95:4.0f}  '
                      f'{self.mode}{" 초과" if over else ""}')

        for b in self.bots:
            b.alive = False
        await asyncio.sleep(0.2)
        self.report()

    def report(self):
        a = self.a
        print('\n===== 결과 =====')
        if self.client_saturated:
            print('  ! 도중에 이 스크립트가 밀렸다(cli > 25ms). 아래 숫자는 서버 한계가 아니라 파이썬')
            print('    한계일 수 있다. 봇을 여러 프로세스로 나눠 다시 재라(--no-viz도 같이).')

        cores_mid = pct(self.fine_cores, 50) if self.fine_cores else None

        if self.fine_counts:
            counts = sorted(self.fine_counts)
            mid = counts[len(counts) // 2]
            print(f'  안정 최대 동접   {mid}명  (한계선 위에서 진동한 인원의 중앙값, '
                  f'{min(counts)}~{max(counts)} 사이)')
        else:
            print(f'  안정 최대 동접   못 찾음. {self.peak}명까지 붙였는데 핑이 기준선 +{a.ping_rise:.0f}ms를 '
                  f'한 번도 안 넘었다.')
            print(f'                   --duration을 늘리거나 --area를 좁혀 밀도를 올려라.')

        if self.trip_point:
            n, p, c = self.trip_point
            cs = f', 그때 서버 {c:.2f}코어' if c is not None else ''
            print(f'  한계 지점        {n}명에서 핑 {p:.1f}ms (기준선 {self.baseline:.1f} + {a.ping_rise:.0f}){cs}')

        # 위 숫자가 진짜 서버 한계인지, 측정 노이즈가 만든 가짜인지 판정해준다.
        if cores_mid is not None:
            print(f'  그때 서버        {cores_mid:.2f}코어 사용')
            if cores_mid > 0.85:
                print(f'  병목             게임 틱 스레드 하나가 코어를 다 썼다. 여기가 진짜 천장이고,')
                print(f'                   coarse playersLock을 쪼개기 전엔 코어를 더 줘도 안 늘어난다.')
                print(f'                   ({self.cores}코어 머신이라 작업관리자엔 '
                      f'{cores_mid / self.cores * 100:.0f}%로만 보인다)')
            elif cores_mid < 0.6:
                print(f'  ! 서버가 코어를 절반도 안 썼는데 한계로 잡혔다. 이건 서버 한계가 아니라')
                print(f'    핑 노이즈(±{a.ping_rise:.0f}ms 안팎)가 만든 가짜 균형점일 가능성이 크다.')
                print(f'    --ping-rise를 올려서(예: {int(a.ping_rise) * 3}) 다시 재라.')
        print(f'  최다 동시         {self.peak}명 | 접속 실패 {self.st.connect_fail}')
        print(f'  누적             수신 {self.st.rx_packets:,} 패킷 / {self.st.rx_bytes / 1048576:.1f} MB')
        print(f'  핑 전체(ms)      p50={pct(self.st.all_pings, 50):.1f} p95={pct(self.st.all_pings, 95):.1f} '
              f'p99={pct(self.st.all_pings, 99):.1f} (틱 대기 0~{IDEAL_TICK_MS:.0f} 포함)')


async def amain(args, vq):
    stats = Stats()
    mon = asyncio.create_task(loop_lag_monitor(stats))
    try:
        await Runner(args, stats, vq).run()
    finally:
        mon.cancel()


def main():
    ap = argparse.ArgumentParser(description='Hamster_RPG 서버 부하 테스트')
    ap.add_argument('--area', type=float, default=25.0, help='봇이 돌아다닐 범위 ±N. 서버 --random-spawn과 같게 (기본 25)')
    ap.add_argument('--ping-rise', type=float, default=10.0, help='기준선보다 이만큼(ms) 오르면 한계로 본다 (기본 10)')
    ap.add_argument('--fine-interval', type=float, default=1.0, help='한계 후 조절 주기 초 (기본 1)')
    ap.add_argument('--warmup-bots', type=int, default=5, help='기준선 측정용 인원 (기본 5)')
    ap.add_argument('--warmup', type=float, default=5.0, help='기준선 측정 시간 초 (기본 5)')
    ap.add_argument('--max-bots', type=int, default=3000, help='안전 상한 (기본 3000)')
    ap.add_argument('--duration', type=float, default=180.0, help='전체 시간 초 (기본 180)')
    ap.add_argument('--no-viz', action='store_true', help='맵 창 없이 측정만')
    ap.add_argument('--host', default='127.0.0.1')
    ap.add_argument('--port', type=int, default=4000)
    ap.add_argument('--proc', default='Server', help='서버 프로세스 이름 (기본 Server)')
    args = ap.parse_args()

    vq, vp = None, None
    if not args.no_viz:
        try:
            import viz
            vq = multiprocessing.Queue(maxsize=3)
            # 뷰어는 별도 프로세스에서 돌린다. 같은 프로세스에서 그리면 그 비용이 핑에 섞인다.
            vp = multiprocessing.Process(target=viz.run, args=(vq, args.area), daemon=True)
            vp.start()
        except Exception as e:
            print(f'맵 창을 못 띄웠다({e}). 측정만 진행한다.')
            vq = None

    try:
        asyncio.run(amain(args, vq))
    except KeyboardInterrupt:
        print('\n중단됨')
    finally:
        if vq:
            try:
                vq.put_nowait('QUIT')
            except Exception:
                pass
        if vp:
            vp.join(timeout=1.0)


if __name__ == '__main__':
    main()
