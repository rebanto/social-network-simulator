cmake_minimum_required(VERSION 3.16)
project(SocialGraph LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release)
endif()

if(MSVC)
    add_compile_options(/W3 /utf-8 /permissive-)
    add_compile_definitions(_CRT_SECURE_NO_WARNINGS)
else()
    add_compile_options(-Wall -Wextra)
endif()

# Portable core: the graph, algorithms, file format, generator, analytics, layout.
add_library(socialgraph_core STATIC
    src/SocialNetwork.cpp
    src/User.cpp
    src/NetworkIO.cpp
    src/Generator.cpp
    src/Analytics.cpp
    src/ForceLayout.cpp
)
target_include_directories(socialgraph_core PUBLIC src)

# Console demo of the core library
add_executable(network_demo src/main.cpp)
target_link_libraries(network_demo PRIVATE socialgraph_core)

# Scale benchmark: bench [users] [averageFriends]
add_executable(bench tools/bench.cpp)
target_link_libraries(bench PRIVATE socialgraph_core)
if(WIN32)
    target_link_libraries(bench PRIVATE psapi)
endif()

# Unit tests
enable_testing()
add_executable(test_core tests/test_core.cpp)
target_link_libraries(test_core PRIVATE socialgraph_core)
add_test(NAME core COMMAND test_core)

# Windows desktop app
if(WIN32)
    enable_language(RC)
    add_executable(SocialGraph WIN32 src/WindowsApp.cpp src/ui/Draw.cpp src/app.rc)
    target_compile_definitions(SocialGraph PRIVATE UNICODE _UNICODE)
    target_link_libraries(SocialGraph PRIVATE socialgraph_core gdiplus comctl32 comdlg32 dwmapi shell32)
    if(MINGW)
        target_link_options(SocialGraph PRIVATE -municode -static)
    elseif(MSVC)
        # app.rc embeds our own manifest (visual styles + per-monitor DPI).
        target_link_options(SocialGraph PRIVATE /MANIFEST:NO)
    endif()
endif()
