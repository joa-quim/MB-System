mb_julia_test(mbnavadjustmerge_test)

# src/mbnavadjust only builds the mbnavadjust GUI executable itself when
# buildGUIs is on (see src/mbnavadjust/CMakeLists.txt); mbnavadjustmerge is
# always built.
if(buildGUIs)
  mb_julia_test(mbnavadjust_test)
endif()
