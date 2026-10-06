# Copyright 2019 Google Inc. All Rights Reserved.
#
# See README file for copying and redistribution conditions.

# Tests for mbhistogram command line app.

include(joinpath(@__DIR__, "..", "mbtest.jl"))

const CMD = mbprog("utilities", "mbhistogram")

@testset "mbhistogram" begin
	@testset "NoArgs" begin
		output, code = mbrun([CMD]; stderr_to_stdout = true)
		@test code == 3
		@test occursin("MBIO Error allocating histogram arrays:", output)
		@test occursin("Illegal format identifier, initialization", output)
		@test occursin("Program <MBHISTOGRAM> Terminated", output)
	end

	@testset "Help" begin
		output = mbcheck([CMD, "-h"]; stderr_to_stdout = true)
		@test occursin("Version", output)
		@test occursin("generates a histogram", output)
		@test occursin("usage:", output)
		@test occursin("-Nnbins", output)
	end

	@testset "HelpVerbose2" begin
		output = mbcheck([CMD, "-h", "-V", "-V"]; stderr_to_stdout = true)
		@test occursin("Version", output)
		@test occursin("generates a histogram", output)
		@test occursin("usage:", output)
		@test occursin("-Nnbins", output)
	end

	# TODO(schwehr): Add tests of actual usage.
end
