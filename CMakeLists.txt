# SPDX-License-Identifier: MIT

project(linux-serial-test C)
cmake_minimum_required(VERSION 3.5)
add_executable(linux-serial-test linux-serial-test.c bother.c)
target_link_libraries(linux-serial-test rt)
install(TARGETS linux-serial-test DESTINATION bin)
