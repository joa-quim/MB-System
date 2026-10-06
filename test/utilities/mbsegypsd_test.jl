# Copyright 2019 Google Inc. All Rights Reserved.
#
# See README file for copying and redistribution conditions.

# Tests for mbsegypsd command line app.

include(joinpath(@__DIR__, "..", "mbtest.jl"))

const CMD = mbprog("utilities", "mbsegypsd")

@testset "mbsegypsd" begin
	@testset "NoArgs" begin
		output = mbcheck([CMD]; stderr_to_stdout = true)
		@test occursin("Bad trace numbers: 0 0 specified", output)
		@test occursin("Terminated", output)
	end

	@testset "Help" begin
		output = mbcheck([CMD, "-h"]; stderr_to_stdout = true)
		@test occursin("Version", output)
		@test occursin("power spectral density function", output)
		@test occursin("usage:", output)
		@test occursin("-Tsweep[/delay]", output)
	end

	@testset "HelpVerbose2" begin
		output = mbcheck([CMD, "-h", "-V", "-V"]; stderr_to_stdout = true)
		@test occursin("Version", output)
		@test occursin("power spectral density function", output)
		@test occursin("usage:", output)
		@test occursin("-Tsweep[/delay]", output)
		@test occursin("dbg2", output)
		@test occursin("frequencyscale:", output)
	end

	# TODO(schwehr): Add tests of actual usage.
end
