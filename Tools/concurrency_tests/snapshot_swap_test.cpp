// 랭킹 스냅샷 교체 검증: 읽는 스레드 8개가 도는 동안 2만 번 바꿔 끼워도 섞인 목록(torn)이 0인지 본다.
// 빌드: cl /std:c++20 /O2 /EHsc snapshot_swap_test.cpp   (리눅스: g++ -std=c++20 -O2 -pthread)
// 2026-11-16 결과: reads=399955 torn=0
#include <atomic>
#include <memory>
#include <vector>
#include <thread>
#include <iostream>
int main(){ using L=std::vector<int>; std::atomic<std::shared_ptr<const L>> snap{std::make_shared<const L>(L(10,0))};
 std::atomic<bool> stop{false}; std::atomic<long> reads{0}, bad{0};
 std::vector<std::thread> readers; for(int t=0;t<8;t++) readers.emplace_back([&]{ while(!stop){ auto p=snap.load(); int v=(*p)[0]; for(int x:*p) if(x!=v) bad++; reads++; } });
 for(int i=1;i<=20000;i++){ snap.store(std::make_shared<const L>(L(10,i))); }
 stop=true; for(auto&t:readers) t.join(); std::cout<<"lock_free="<<snap.is_lock_free()<<" reads="<<reads<<" torn="<<bad<<"\n"; }
