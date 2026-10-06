set(tests mbgrd2gltf_test)

foreach(test ${tests})
  mb_julia_test(${test})
endforeach()
