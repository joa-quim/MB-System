set(tests dump_gsf_test)

foreach(test ${tests})
  mb_julia_test(${test})
endforeach()
