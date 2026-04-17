1
cmake_minimum_required(VERSION 3.21)
2
set(CMAKE_CXX_STANDARD 20)
3
set(CMAKE_CXX_STANDARD_REQUIRED ON)
4
set(CMAKE_OSX_ARCHITECTURES "x86_64")
5
6
# ---- Проект ----
7
project(GDNeuralBot VERSION 1.0.0)
8
9
# ---- Geode SDK ----
10
# Убедитесь что GEODE_SDK указывает на папку с Geode SDK
11
if (NOT DEFINED ENV{GEODE_SDK})
12
    message(FATAL_ERROR "GEODE_SDK environment variable not 21. "
13
            "Install Geode CLI and run: geode sdk install")
14
endif()
15
16
set(GEODE_SDK $ENV{GEODE_SDK})
17
include(${GEODE_SDK}/cmake/GeodeFile.cmake)
18
19
# ---- Исходники ----
20
file(GLOB_RECURSE SOURCES
21
    src/*.cpp
22
    src/*.hpp
23
)
24
25
add_library(${PROJECT_NAME} SHARED ${SOURCES})
26
27
# ---- Включаем заголовки ----
28
target_include_directories(${PROJECT_NAME} PRIVATE
29
    src/
30
    ${GEODE_SDK}/include
31
)
32
33
# ---- Опции компилятора ----
34
target_compile_options(${PROJECT_NAME} PRIVATE
35
    $<$<CXX_COMPILER_ID:MSVC>:/W4 /O2 /fp:fast>
36
    $<$<NOT:$<CXX_COMPILER_ID:MSVC>>:-Wall -O2 -ffast-math>
37
)
38
39
# ---- Geode Setup (создаёт .geode файл) ----
40
setup_geode_mod(${PROJECT_NAME})
41
42
# ---- Информация о сборке ----
43
message(STATUS "=================================")
44
message(STATUS " GD Neural Bot — Build Config")
45
message(STATUS " Version: 54")
46
message(STATUS " GEODE_SDK: 55")
47
message(STATUS " C++ Standard: 56")
48
message(STATUS "=================================")
49
50
# ---- Установка ----
51
# После сборки .geode файл автоматически копируется в GD/geode/mods/
52
# если в Geode CLI настроен профиль
