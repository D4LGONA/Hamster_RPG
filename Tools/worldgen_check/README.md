# WorldGen 일치 검사

서버 `Server/WorldGen.h` 와 클라 `Assets/Scripts/World/WorldGen.cs` 가 같은 세계를 만드는지 확인한다.
81×81 청크의 장애물·몬스터 자리와, 원점 근처 충돌 판정 결과를 텍스트로 뽑아 비교한다.

```bat
:: Visual Studio 개발자 명령 프롬프트에서 (이 폴더 기준)
cl /std:c++20 /EHsc /O2 dump_cpp.cpp /Fe:dump_cpp.exe
csc /nologo DumpCs.cs ..\..\Assets\Scripts\World\WorldGen.cs /out:dump_cs.exe
dump_cpp.exe > cpp.txt
dump_cs.exe  > cs.txt
fc cpp.txt cs.txt
```

`fc` 가 "차이점이 없습니다" 라고 하면 통과. 공식을 고치면 꼭 다시 돌린다.
(2026-11-12 기준 41,949줄 일치 확인)
