# Copyright 2019 Google Inc. All Rights Reserved.
#
# See README file for copying and redistribution conditions.

# Tests for mbswplspreprocess command line app.

include(joinpath(@__DIR__, "..", "mbtest.jl"))

const CMD = mbprog("deprecated", "mbswplspreprocess")

@testset "mbswplspreprocess" begin
	@testset "NoArgs" begin
		output, code = mbrun([CMD]; stderr_to_stdout = true)
		@test code == 2
		@test occursin("MBIO Error returned from function", output)
		@test occursin("mb_datalist_open", output)
		@test occursin("Unable to open data list file: datalist.mb-1", output)
	end

	@testset "Help" begin
		output = mbcheck([CMD, "-h"]; stderr_to_stdout = true)
		@test occursin("Version", output)
		@test occursin("Preprocess SWATHplus SXP formatted files", output)
		@test occursin("-Obasename", output)
	end

	@testset "HelpVerbose2" begin
		output = mbcheck([CMD, "-h", "-V", "-V"]; stderr_to_stdout = true)
		@test occursin("Version", output)
		@test occursin("Preprocess SWATHplus SXP formatted files", output)
		@test occursin("-Obasename", output)
		@test occursin("dbg2", output)
		@test occursin("lonflip", output)
		@test occursin("copy_rawamp:", output)
	end

	# TODO(schwehr): Add tests of actual usage.
end
