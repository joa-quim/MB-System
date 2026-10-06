# Copyright 2019 Google Inc. All Rights Reserved.
#
# See README file for copying and redistribution conditions.

# Tests for mbcopy command line app.

include(joinpath(@__DIR__, "..", "mbtest.jl"))

const CMD = mbprog("utilities", "mbcopy")

@testset "mbcopy" begin
	@testset "NoArgs" begin
		output, code = mbrun([CMD]; stderr_to_stdout = true)
		@test code == 3
		@test occursin("MBIO Error returned from function <mb_format>", output)
		@test occursin("initialization failed", output)
	end

	@testset "Help" begin
		output = mbcheck([CMD, "-h"]; stderr_to_stdout = true)
		@test occursin("Version", output)
		@test occursin("copies an input swath sonar data file to an output", output)
		@test occursin("-Qsleep_factor", output)
	end

	@testset "HelpVerbose2" begin
		output = mbcheck([CMD, "-h", "-V", "-V"]; stderr_to_stdout = true)
		@test occursin("Version", output)
		@test occursin("copies an input swath sonar data file to an output", output)
		@test occursin("-Qsleep_factor", output)
		@test occursin("dbg2", output)
		@test occursin("lonflip", output)
		@test occursin("fbtversion:", output)
	end

	@testset "TooManyStripModes" begin
		output, code = mbrun([CMD, "-n", "-n", "-n"]; stderr_to_stdout = true)
		if code == 0
			@test isempty(output)  # Should always fail if it gets here.
		else
			@test code == 9
			@test occursin("Gave -n more than twice.", output)
		end
	end

	# TODO(schwehr): Add tests of actual usage.
end
