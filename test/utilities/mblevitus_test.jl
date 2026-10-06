# Copyright 2019 Google Inc. All Rights Reserved.
#
# See README file for copying and redistribution conditions.

# Tests for mblevitus command line app.

include(joinpath(@__DIR__, "..", "mbtest.jl"))

const CMD = mbprog("utilities", "mblevitus")

@testset "mblevitus" begin
	@testset "Help" begin
		output = mbcheck([CMD, "-h"]; stderr_to_stdout = true)
		@test occursin("Version", output)
		@test occursin("generates an average water velocity profile", output)
		@test occursin("usage:", output)
		@test occursin("-Rlon/lat", output)
	end

	@testset "HelpVerbose2" begin
		output = mbcheck([CMD, "-h", "-V", "-V"]; stderr_to_stdout = true)
		@test occursin("Version", output)
		@test occursin("generates an average water velocity profile", output)
		@test occursin("usage:", output)
		@test occursin("-Rlon/lat", output)
		@test occursin("dbg2", output)
		@test occursin("levitusfile:", output)
	end

	# TODO(schwehr): Add tests of actual usage.
end
