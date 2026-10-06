# Copyright 2019 Google Inc. All Rights Reserved.
#
# See README file for copying and redistribution conditions.

# Tests for mbminirovnav command line app.

include(joinpath(@__DIR__, "..", "mbtest.jl"))

const CMD = mbprog("utilities", "mbminirovnav")

@testset "mbminirovnav" begin
	@testset "NoArgs" begin
		output, code = mbrun([CMD]; stderr_to_stdout = true)
		# This might not fail; when it does, it is with exit code 1.
		code == 0 || @test code == 1
	end

	@testset "Help" begin
		output = mbcheck([CMD, "--help"]; stderr_to_stdout = true)
		@test occursin("Version", output)
		@test occursin("USBL tracking and CTD day files from the MBARI", output)
		@test occursin("usage:", output)
		@test occursin("--rov-dive-start=yyyymmddhhmmss", output)
	end

	@testset "HelpVerbose2" begin
		output = mbcheck([CMD, "--help", "--verbose", "--verbose"]; stderr_to_stdout = true)
		@test occursin("Version", output)
		@test occursin("USBL tracking and CTD day files from the MBARI", output)
		@test occursin("usage:", output)
		@test occursin("--rov-dive-start=yyyymmddhhmmss", output)
	end

	# TODO(schwehr): Add tests of actual usage.
end
