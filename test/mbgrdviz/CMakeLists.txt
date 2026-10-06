set(tests mbgrdviz_test)

foreach(test ${tests})
  mb_julia_test(${test})
endforeach()
