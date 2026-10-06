# Copyright 2019 Google Inc. All Rights Reserved.
#
# See README file for copying and redistribution conditions.

# Tests for mbbackangle command line app.

include(joinpath(@__DIR__, "..", "mbtest.jl"))

const CMD = mbprog("utilities", "mbbackangle")

@testset "mbbackangle" begin
	@testset "NoArgs" begin
		output, code = mbrun([CMD]; stderr_to_stdout = true)
		@test code == 2
		@test occursin("Unable to open data list file:", output)
		@test occursin("datalist.mb-1", output)
	end

	@testset "Help" begin
		output = mbcheck([CMD, "-h"]; stderr_to_stdout = true)
		@test occursin("Version", output)
		@test occursin("reads a swath sonar data file", output)
		@test occursin("tables containing the average amplitude", output)
	end

	@testset "HelpVerbose2" begin
		output = mbcheck([CMD, "-h", "-V", "-V"]; stderr_to_stdout = true)
		@test occursin("Version", output)
		@test occursin("reads a swath sonar data file", output)
		@test occursin("tables containing the average amplitude", output)
	end

	# TODO(schwehr): Add tests of actual usage.
end
