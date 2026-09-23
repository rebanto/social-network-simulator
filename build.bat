@echo off
rem Builds the Social Graph desktop app and the console demo with MinGW-w64 (e.g. MSYS2 mingw64).
setlocal
if not exist build mkdir build

windres -I src src\app.rc -O coff -o build\app.o || exit /b 1
g++ -std=c++17 -O2 -municode -mwindows src\WindowsApp.cpp src\SocialNetwork.cpp src\User.cpp build\app.o ^
    -o build\SocialGraph.exe -lgdiplus -lcomctl32 -ldwmapi -static || exit /b 1
g++ -std=c++17 -O2 src\main.cpp src\SocialNetwork.cpp src\User.cpp -o build\network_demo.exe -static || exit /b 1

echo Built build\SocialGraph.exe and build\network_demo.exe
