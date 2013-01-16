project (linux-serial-test)
add_executable(linux-serial-test linux-serial-test.c)
target_link_libraries(linux-serial-test rt)
install(TARGETS linux-serial-test DESTINATION bin)

