# Copyright 2019 Google Inc. All Rights Reserved.
#
# See README file for copying and redistribution conditions.

# Tests for mb7k2ss command line app.

include(joinpath(@__DIR__, "..", "mbtest.jl"))

const CMD = mbprog("deprecated", "mb7k2ss")

@testset "mb7k2ss" begin
	@testset "NoArgs" begin
		output, code = mbrun([CMD]; stderr_to_stdout = true)
		@test code == 2
		@test occursin("Low Sidescan", output)
		@test occursin("Unable to open data list file:", output)
		@test occursin("datalist.mb-1", output)
	end

	@testset "Help" begin
		output, code = mbrun([CMD, "-h"]; stderr_to_stdout = true)
		@test code == 1
		@test occursin("mb7k2ss extracts sidescan sonar", output)
		@test occursin("MBF_MBLDEOIH", output)
	end

	@testset "HelpVerbose2" begin
		output, code = mbrun([CMD, "-h", "-V", "-V"]; stderr_to_stdout = true)
		@test code == 1
		@test occursin("mb7k2ss extracts sidescan sonar", output)
		@test occursin("dbg2", output)
		@test occursin("lonflip", output)
	end

	# TODO(schwehr): Add tests of actual usage.
end
