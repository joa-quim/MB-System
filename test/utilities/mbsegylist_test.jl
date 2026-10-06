# Copyright 2019 Google Inc. All Rights Reserved.
#
# See README file for copying and redistribution conditions.

# Tests for mbsegylist command line app.

include(joinpath(@__DIR__, "..", "mbtest.jl"))

const CMD = mbprog("utilities", "mbsegylist")

@testset "mbsegylist" begin
	@testset "NoArgs" begin
		output, code = mbrun([CMD]; stderr_to_stdout = true)
		@test code == 2
		@test occursin("Unable to open segy file", output)
		@test occursin("mb_segy_read_init", output)
		@test occursin("SEGY File <> not initialized for reading", output)
	end

	@testset "Help" begin
		output = mbcheck([CMD, "-h"]; stderr_to_stdout = true)
		@test occursin("Version", output)
		@test occursin("lists table data from a segy data file", output)
		@test occursin("usage:", output)
		@test occursin("-Gcharacter", output)
	end

	@testset "HelpVerbose2" begin
		output = mbcheck([CMD, "-h", "-V", "-V"]; stderr_to_stdout = true)
		@test occursin("Version", output)
		@test occursin("lists table data from a segy data file", output)
		@test occursin("usage:", output)
		@test occursin("-Gcharacter", output)
		@test occursin("dbg2", output)
		@test occursin("lonflip", output)
		@test occursin("segment_tag:", output)
	end

	# TODO(schwehr): Add tests of actual usage.
end
