# Copyright 2019 Google Inc. All Rights Reserved.
#
# See README file for copying and redistribution conditions.

# Tests for mbpreprocess command line app.

include(joinpath(@__DIR__, "..", "mbtest.jl"))

const CMD = mbprog("utilities", "mbpreprocess")

@testset "mbpreprocess" begin
	@testset "NoArgs" begin
		output, code = mbrun([CMD]; stderr_to_stdout = true)
		@test code == 3
		@test occursin("MBIO Error returned from function", output)
		@test occursin("Illegal format identifier", output)
		@test occursin("Multibeam File <> not initialized for reading", output)
	end

	@testset "Help" begin
		output = mbcheck([CMD, "--help"]; stderr_to_stdout = true)
		@test occursin("Version", output)
		@test occursin("handles preprocessing of swath sonar data", output)
		@test occursin("--platform-file=platform_file", output)
		@test occursin("--kluge-fix-wissl-timestamps", output)
	end

	@testset "HelpVerbose2" begin
		output = mbcheck([CMD, "--help", "--verbose", "--verbose"]; stderr_to_stdout = true)
		@test occursin("Version", output)
		@test occursin("handles preprocessing of swath sonar data", output)
		@test occursin("--platform-file=platform_file", output)
		@test occursin("--kluge-fix-wissl-timestamps", output)
	end

	# TODO(schwehr): Add tests of actual usage.
end
