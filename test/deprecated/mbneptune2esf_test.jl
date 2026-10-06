# Copyright 2019 Google Inc. All Rights Reserved.
#
# See README file for copying and redistribution conditions.

# Tests for mbneptune2esf command line app.

include(joinpath(@__DIR__, "..", "mbtest.jl"))

const CMD = mbprog("deprecated", "mbneptune2esf")

@testset "mbneptune2esf" begin
	# testNoArgs hangs.

	@testset "Help" begin
		output = mbcheck([CMD, "-h"]; stderr_to_stdout = true)
		@test occursin("Version", output)
		@test occursin("Simrad Neptune BinStat rules file", output)
		@test occursin("usage:", output)
		@test occursin("-Rrules", output)
	end

	@testset "HelpVerbose2" begin
		output = mbcheck([CMD, "-h", "-V", "-V"]; stderr_to_stdout = true)
		@test occursin("Version", output)
		@test occursin("Simrad Neptune BinStat rules file", output)
		@test occursin("usage:", output)
		@test occursin("-Rrules", output)
		@test occursin("dbg2", output)
		@test occursin("lonflip", output)
		@test occursin("speedmin:", output)
	end

	# TODO(schwehr): Add tests of actual usage.
end
