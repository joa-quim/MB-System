# Copyright 2019 Google Inc. All Rights Reserved.
#
# See README file for copying and redistribution conditions.

# Tests for mbauvnavusbl command line app.

include(joinpath(@__DIR__, "..", "mbtest.jl"))

const CMD = mbprog("deprecated", "mbauvnavusbl")

@testset "mbauvnavusbl" begin
	@testset "NoArgs" begin
		output, code = mbrun([CMD]; stderr_to_stdout = true)
		@test code == 2
		@test occursin("Unable to Open Navigation File <stdin> for", output)
	end

	@testset "Help" begin
		output = mbcheck([CMD, "-h"]; stderr_to_stdout = true)
		@test occursin("Version", output)
		@test occursin("USBL fixes", output)
		@test occursin("reads a primary navigation file", output)
		@test occursin("-Uusblfile", output)
	end

	@testset "HelpVerbose2" begin
		output = mbcheck([CMD, "-h", "-V", "-V"]; stderr_to_stdout = true)
		@test occursin("Version", output)
		@test occursin("USBL fixes", output)
		@test occursin("reads a primary navigation file", output)
		@test occursin("-Uusblfile", output)
		@test occursin("dbg2", output)
		@test occursin("lonflip", output)
		@test occursin("usbl format:", output)
	end

	# TODO(schwehr): Add tests of actual usage.
end
