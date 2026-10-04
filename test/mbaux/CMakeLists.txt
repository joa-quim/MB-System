message("In test/mbaux")

set(tests mb_cube_test)

foreach(test ${tests})
  add_executable(${test} ${test}.cc)
  target_compile_features(${test} PRIVATE cxx_std_17)
  target_include_directories(${test} PUBLIC ${CMAKE_CURRENT_SOURCE_DIR} ../../src)
  target_link_libraries(${test} PRIVATE mbaux GTest::gmock_main)
  add_test(NAME ${test} COMMAND ${test})
endforeach()
