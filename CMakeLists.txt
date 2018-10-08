cmake_minimum_required(VERSION 3.5)
project(speed_test)

set(CMAKE_CXX_STANDARD 11)

set(SOURCE_SERVER server.cpp packet.cpp packet.h)
set(SOURCE_CLIENT client.cpp packet.cpp packet.h client_lib.cpp client_lib.h)

add_executable(speed_test_server ${SOURCE_SERVER})
add_executable(speed_test_client ${SOURCE_CLIENT})
