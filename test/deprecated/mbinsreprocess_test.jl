# Copyright 2019 Google Inc. All Rights Reserved.
#
# See README file for copying and redistribution conditions.

# Tests for mbinsreprocess command line app.

include(joinpath(@__DIR__, "..", "mbtest.jl"))

const CMD = mbprog("deprecated", "mbinsreprocess")

@testset "mbinsreprocess" begin
	@testset "NoArgs" begin
		output, code = mbrun([CMD]; stderr_to_stdout = true)
		@test code == 1
		@test occursin("Unable to open log file <stdin> for reading", output)
	end

	@testset "Help" begin
		output = mbcheck([CMD, "--help"]; stderr_to_stdout = true)
		@test occursin("Version", output)
		@test occursin("reads an INS navigation file", output)
		@test occursin("usage:", output)
		@test occursin("--output=filename", output)
	end

	@testset "HelpVerbose2" begin
		output = mbcheck([CMD, "--help", "--verbose", "--verbose"]; stderr_to_stdout = true)
		@test occursin("Version", output)
		@test occursin("reads an INS navigation file", output)
		@test occursin("usage:", output)
		@test occursin("--output=filename", output)
		@test occursin("dbg2", output)
		@test occursin("lonflip", output)
		@test occursin("ofile:", output)
	end

	# TODO(schwehr): Add tests of actual usage.
end
