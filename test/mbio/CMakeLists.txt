##find_package(GTest REQUIRED)
message("In test/mbio")

set(tests mb_defaults_test mb_error_test mb_format_test mb_mem_test
          mb_read_init_test mb_time_test)

foreach(test ${tests})
  add_executable(${test} ${test}.cc)
  target_compile_features(${test} PRIVATE cxx_std_17)
  target_include_directories(${test} PUBLIC ${CMAKE_CURRENT_SOURCE_DIR} ../../src)
  target_link_libraries(${test} PRIVATE mbio GTest::gmock_main)
  add_test(NAME ${test} COMMAND ${test})
endforeach()

# mb_mem_test puts a struct mb_io_struct (about 1.05 MB) on the stack. That fits the 8 MB
# default stack of Linux and macOS but not the 1 MB one of MSVC executables, where the test
# dies with a stack overflow (0xC00000FD) before running. Give it the Linux size.
if(MSVC)
  target_link_options(mb_mem_test PRIVATE /STACK:8388608)
endif()
