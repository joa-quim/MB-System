# Copyright 2019 Google Inc. All Rights Reserved.
#
# See README file for copying and redistribution conditions.

# Tests for mbrollbias command line app.

include(joinpath(@__DIR__, "..", "mbtest.jl"))

const CMD = mbprog("deprecated", "mbrollbias")

@testset "mbrollbias" begin
	@testset "NoArgs" begin
		output, code = mbrun([CMD]; stderr_to_stdout = true)
		@test code == 12
		@test occursin("Grid bounds not properly specified:", output)
		@test occursin("0.0", output)
	end

	@testset "Help" begin
		output = mbcheck([CMD, "-h"]; stderr_to_stdout = true)
		@test occursin("Version", output)
		@test occursin("assess roll bias of swath", output)
		@test occursin("starboard", output)
		@test occursin("usage:", output)
		@test occursin("-Jfile2", output)
	end

	@testset "HelpVerbose2" begin
		output = mbcheck([CMD, "-h", "-V", "-V"]; stderr_to_stdout = true)
		@test occursin("Version", output)
		@test occursin("assess roll bias of swath", output)
		@test occursin("starboard", output)
		@test occursin("usage:", output)
		@test occursin("-Jfile2", output)
		@test occursin("dbg2", output)
		@test occursin("lonflip", output)
		@test occursin("grid y dimension:", output)
	end

	# TODO(schwehr): Add tests of actual usage.
end
