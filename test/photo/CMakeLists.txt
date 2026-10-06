set(tests
    mbgetphotocorrection_test
    mbimagecorrect_test
    mbimagelist_test
    mbphotogrammetry_test
    mbphotomosaic_test
    mbtiff2png_test)

foreach(test ${tests})
  mb_julia_test(${test})
endforeach()
