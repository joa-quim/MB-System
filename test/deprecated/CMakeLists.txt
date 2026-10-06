set(tests
    mb7k2ss_test
    mb7kpreprocess_test
    mbauvnavusbl_test
    mbhsdump_test
    mbhysweeppreprocess_test
    mbinsreprocess_test
    mbkongsbergpreprocess_test
    mbneptune2esf_test
    mbrollbias_test
    mbswplspreprocess_test)

foreach(test ${tests})
  mb_julia_test(${test})
endforeach()
