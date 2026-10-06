# Copyright 2019 Google Inc. All Rights Reserved.
#
# See README file for copying and redistribution conditions.

# Tests for mbsslayout command line app.

include(joinpath(@__DIR__, "..", "mbtest.jl"))

const CMD = mbprog("utilities", "mbsslayout")

@testset "mbsslayout" begin
	@testset "NoArgs" begin
		output, code = mbrun([CMD]; stderr_to_stdout = true)
		@test code == 2
		@test occursin("Unable to open data list file:", output)
		@test occursin("datalist.mb-1", output)
	end

	@testset "Help" begin
		output = mbcheck([CMD, "--help"]; stderr_to_stdout = true)
		@test occursin("Version", output)
		@test occursin("reads sidescan in raw time series form", output)
		@test occursin("usage:", output)
		@test occursin("--input=datalist", output)
	end

	@testset "HelpVerbose2" begin
		output = mbcheck([CMD, "--help", "--verbose", "--verbose"]; stderr_to_stdout = true)
		@test occursin("Version", output)
		@test occursin("reads sidescan in raw time series form", output)
		@test occursin("usage:", output)
		@test occursin("--input=datalist", output)
		@test occursin("dbg2", output)
		@test occursin("lonflip", output)
		@test occursin("time_latency_apply:", output)
	end

	# TODO(schwehr): Add tests of actual usage.
end
