cmake_minimum_required(VERSION 3.16)
project(SocialNetworkVisualizer LANGUAGES CXX RC)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(CORE_SOURCES src/SocialNetwork.cpp src/User.cpp)

# Windows desktop front-end
add_executable(SocialGraph WIN32 src/WindowsApp.cpp src/app.rc ${CORE_SOURCES})
target_compile_definitions(SocialGraph PRIVATE UNICODE _UNICODE)
target_include_directories(SocialGraph PRIVATE src)
target_link_libraries(SocialGraph PRIVATE gdiplus comctl32 dwmapi)
if(MINGW)
    target_link_options(SocialGraph PRIVATE -municode -static)
elseif(MSVC)
    target_link_options(SocialGraph PRIVATE /MANIFEST:NO)
endif()

# Console demo of the core library
add_executable(network_demo src/main.cpp ${CORE_SOURCES})
