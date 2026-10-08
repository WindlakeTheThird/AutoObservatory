cmake_minimum_required(VERSION 3.16)
project(Autobservatory LANGUAGES C CXX)

set(CMAKE_C_STANDARD 99)
set(CMAKE_CXX_STANDARD 17)

add_executable(autobservatory
    main.cpp
    wmm_mag.c
)

target_include_directories(autobservatory PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(autobservatory PRIVATE m)
