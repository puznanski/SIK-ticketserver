cmake_minimum_required(VERSION 3.21)
project(SIK_ticketserver)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_FLAGS "-Wall -Wextra -Wno-implicit-fallthrough -std=c++17 -O2")
set(CMAKE_EXE_LINKER_FLAGS "-Wall -Wextra -Wno-implicit-fallthrough -std=c++17 -O2")

add_executable(ticket_server ticket_server.cpp)
