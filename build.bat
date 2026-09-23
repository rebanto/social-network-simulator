@echo off
rem Builds Social Graph with MinGW-w64 (e.g. MSYS2 mingw64): the desktop app,
rem the console demo, the unit tests and the scale benchmark. Runs the tests.
setlocal
if not exist build mkdir build

set CORE=src\SocialNetwork.cpp src\User.cpp src\NetworkIO.cpp src\Generator.cpp src\Analytics.cpp src\ForceLayout.cpp
set FLAGS=-std=c++17 -O2 -Wall -Wextra -Isrc -static

windres -I src src\app.rc -O coff -o build\app.o || exit /b 1
g++ %FLAGS% -municode -mwindows src\WindowsApp.cpp src\ui\Draw.cpp %CORE% build\app.o ^
    -o build\SocialGraph.exe -lgdiplus -lcomctl32 -lcomdlg32 -ldwmapi -lshell32 || exit /b 1
g++ %FLAGS% src\main.cpp %CORE% -o build\network_demo.exe || exit /b 1
g++ %FLAGS% tests\test_core.cpp %CORE% -o build\test_core.exe || exit /b 1
g++ %FLAGS% tools\bench.cpp %CORE% -o build\bench.exe -lpsapi || exit /b 1

echo Built build\SocialGraph.exe, build\network_demo.exe, build\test_core.exe, build\bench.exe
build\test_core.exe || exit /b 1
