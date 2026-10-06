# Copyright 2019 Google Inc. All Rights Reserved.
#
# See README file for copying and redistribution conditions.

# Tests for mbsvpselect command line app.

include(joinpath(@__DIR__, "..", "mbtest.jl"))

const CMD = mbprog("utilities", "mbsvpselect")

@testset "mbsvpselect" begin
	# testNoArgs needs input.

	@testset "Help" begin
		output = mbcheck([CMD, "-h"]; stderr_to_stdout = true)
		@test occursin("Version", output)
		@test occursin("chooses and implements the best available sound", output)
		@test occursin("usage:", output)
		@test occursin("-Pmode[/period_or_range[/seasonal]]", output)
	end

	@testset "HelpVerbose2" begin
		output = mbcheck([CMD, "-h", "-V", "-V"]; stderr_to_stdout = true)
		@test occursin("Version", output)
		@test occursin("chooses and implements the best available sound", output)
		@test occursin("usage:", output)
		@test occursin("-Pmode[/period_or_range[/seasonal]]", output)
		@test occursin("dbg2", output)
		@test occursin("svplist:", output)
		@test occursin("zero_test:", output)
	end

	# TODO(schwehr): Add tests of actual usage.
end
