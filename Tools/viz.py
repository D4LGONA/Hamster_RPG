"""부하 테스트 실시간 맵 뷰어 (loadtest.py가 별도 프로세스로 띄운다).

봇 프로세스와 분리해서 돌린다. 같은 프로세스에서 그리면 그리는 비용이 핑에 섞여서,
서버가 아니라 tkinter를 재는 꼴이 되기 때문이다.

    하얀 점  플레이어(봇)
    빨간 점  몬스터
"""

import queue
import tkinter as tk

BG = '#0a0a0f'
GRID = '#1c1c28'
PLAYER = '#ffffff'
MONSTER = '#ff3b30'
TEXT = '#c8c8d0'

CANVAS = 720
MARGIN = 30


def run(q, area):
    """q: 메인 프로세스가 0.1초마다 넣어주는 스냅샷. area: 월드 표시 범위 ±area."""
    root = tk.Tk()
    root.title('Hamster_RPG 부하 테스트')
    root.configure(bg=BG)

    canvas = tk.Canvas(root, width=CANVAS, height=CANVAS, bg=BG, highlightthickness=0)
    canvas.pack(padx=8, pady=(8, 0))

    info = tk.Label(root, text='연결 대기중...', font=('Consolas', 11), fg=TEXT, bg=BG,
                    justify='left', anchor='w')
    info.pack(fill='x', padx=12, pady=(6, 10))

    span = area * 1.15   # 봇이 area를 살짝 넘어가도 화면 안에 남게 여유를 둔다
    inner = CANVAS - MARGIN * 2

    def to_screen(x, y):
        sx = MARGIN + (x + span) / (2 * span) * inner
        sy = MARGIN + (span - y) / (2 * span) * inner   # 화면 y는 아래로 증가하므로 뒤집는다
        return sx, sy

    # 배경 격자: 서버의 섹터 크기(50)와 시야(20)를 눈금으로 깔아둔다
    def draw_grid():
        step = 50.0 if area > 60 else 20.0
        v = -int(span / step) * step
        while v <= span:
            sx, _ = to_screen(v, 0)
            _, sy = to_screen(0, v)
            canvas.create_line(sx, MARGIN, sx, CANVAS - MARGIN, fill=GRID)
            canvas.create_line(MARGIN, sy, CANVAS - MARGIN, sy, fill=GRID)
            v += step
        canvas.create_rectangle(MARGIN, MARGIN, CANVAS - MARGIN, CANVAS - MARGIN, outline='#2a2a3a')
        canvas.create_text(CANVAS - MARGIN, CANVAS - MARGIN + 14, anchor='e', fill='#555',
                           font=('Consolas', 9), text=f'격자 {step:.0f} · 표시범위 ±{area:.0f}')

    draw_grid()
    dots = []   # 재사용할 캔버스 아이템. 매번 지웠다 만들면 점이 많을 때 버벅인다.

    def draw(snap):
        players = snap['players']
        monsters = snap['monsters']
        need = len(players) + len(monsters)

        while len(dots) < need:
            dots.append(canvas.create_oval(0, 0, 0, 0, outline=''))

        i = 0
        for x, y in players:
            sx, sy = to_screen(x, y)
            canvas.coords(dots[i], sx - 2, sy - 2, sx + 2, sy + 2)
            canvas.itemconfig(dots[i], fill=PLAYER, state='normal')
            i += 1
        for x, y in monsters:
            sx, sy = to_screen(x, y)
            canvas.coords(dots[i], sx - 4, sy - 4, sx + 4, sy + 4)
            canvas.itemconfig(dots[i], fill=MONSTER, state='normal')
            i += 1
        for j in range(i, len(dots)):
            canvas.itemconfig(dots[j], state='hidden')

        info.config(text=snap['info'])

    def poll():
        snap = None
        try:
            while True:            # 밀렸으면 중간 프레임은 버리고 최신 것만 그린다
                snap = q.get_nowait()
        except queue.Empty:
            pass
        if snap == 'QUIT':
            root.destroy()
            return
        if snap:
            draw(snap)
        root.after(50, poll)

    root.after(50, poll)
    root.mainloop()
