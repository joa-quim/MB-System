# Copyright 2019 Google Inc. All Rights Reserved.
#
# See README file for copying and redistribution conditions.

# Tests for mbnavlist command line app.

include(joinpath(@__DIR__, "..", "mbtest.jl"))

const CMD = mbprog("utilities", "mbnavlist")

@testset "mbnavlist" begin
	@testset "NoArgs" begin
		output, code = mbrun([CMD]; stderr_to_stdout = true)
		@test code == 2
		@test occursin("Unable to open data list file: datalist.mb-1", output)
	end

	@testset "Help" begin
		output = mbcheck([CMD, "-h"]; stderr_to_stdout = true)
		@test occursin("mbnavlist prints", output)
	end

	@testset "DoesNotExist" begin
		filename = "/does/not/exist.mb21"
		output, code = mbrun([CMD, "-I" * filename]; stderr_to_stdout = true)
		@test code == 2
		@test occursin("Unable to open file", output)
		@test occursin(filename, output)
	end

	# Every testdata/mb*/<file>.mbnavlist.txt is the expected listing of testdata/mb*/<file>
	@testset "NavListOutput" begin
		expected_files = [joinpath("testdata", d, f) for d in readdir("testdata") if startswith(d, "mb")
		                  for f in readdir(joinpath("testdata", d)) if endswith(f, ".mbnavlist.txt")]
		@test !isempty(expected_files)
		for expected_filename in expected_files
			src_filename = expected_filename[1:end - length(".mbnavlist.txt")]
			output = mbcheck([CMD, "-I" * src_filename])
			@test unixlines(output) == unixlines(read(expected_filename, String))
		end
	end
end
