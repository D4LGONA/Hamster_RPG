// 2026-06-11 : stdafx.h 파일 제작. 미리 컴파일된 헤더.
#pragma once

// Windows / Winsock
#include <WinSock2.h>
#include <WS2tcpip.h>
#include <MSWSock.h>
#include <Windows.h>

#pragma comment(lib, "WS2_32.lib")
#pragma comment(lib, "MSWSock.lib")

// C++ Standard Library
#include <iostream>
#include <iomanip>
#include <cstdint>
#include <array>
#include <vector>
#include <queue>
#include <unordered_map>
#include <string>
#include <memory>
#include <mutex>
#include <atomic>
#include <thread>
#include <chrono>
#include <algorithm>
#include <cstring>
#include <cstdlib>
#include <cmath>
#include <random>