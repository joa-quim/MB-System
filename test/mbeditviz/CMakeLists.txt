set(tests mbeditviz_test)

foreach(test ${tests})
  mb_julia_test(${test})
endforeach()
