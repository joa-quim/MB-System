# Copyright 2019 Google Inc. All Rights Reserved.
#
# See README file for copying and redistribution conditions.

# Tests for mbdumpesf command line app.

include(joinpath(@__DIR__, "..", "mbtest.jl"))

const CMD = mbprog("utilities", "mbdumpesf")

@testset "mbdumpesf" begin
	@testset "NoArgs" begin
		# mbdumpesf now requires --input=esffile and exits with an error
		# instead of silently running with an uninitialized filename.
		output, code = mbrun([CMD]; stderr_to_stdout = true)
		@test code == 2
		@test occursin("No input edit save file specified", output)
		@test occursin("usage:", output)
	end

	@testset "Help" begin
		output = mbcheck([CMD, "-h"]; stderr_to_stdout = true)
		@test occursin("edit save file and dumps", output)
		@test occursin("as an ascii table to stdout", output)
		@test occursin("usage:", output)
		@test occursin("--ignore-unflag", output)
	end

	@testset "HelpVerbose2" begin
		output = mbcheck([CMD, "-h", "-V", "-V"]; stderr_to_stdout = true)
		@test occursin("edit save file and dumps", output)
		@test occursin("as an ascii table to stdout", output)
		@test occursin("usage:", output)
		@test occursin("--ignore-unflag", output)
		# -V -V does not do anything.
		@test !occursin("dbg2", output)
	end

	# TODO(schwehr): Add tests of actual usage.
end
