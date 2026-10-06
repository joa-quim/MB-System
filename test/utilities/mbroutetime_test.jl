# Copyright 2019 Google Inc. All Rights Reserved.
#
# See README file for copying and redistribution conditions.

# Tests for mbroutetime command line app.

include(joinpath(@__DIR__, "..", "mbtest.jl"))

const CMD = mbprog("utilities", "mbroutetime")

@testset "mbroutetime" begin
	@testset "NoArgs" begin
		output = mbcheck([CMD]; stderr_to_stdout = true)
		@test occursin("Unable to open route file <> for reading", output)
	end

	@testset "Help" begin
		output = mbcheck([CMD, "-h"]; stderr_to_stdout = true)
		@test occursin("Version", output)
		@test occursin("list of the times when a survey hit the waypoints", output)
		@test occursin("usage:", output)
		@test occursin("-Urangethreshold", output)
	end

	@testset "HelpVerbose2" begin
		output = mbcheck([CMD, "-h", "-V", "-V"]; stderr_to_stdout = true)
		@test occursin("Version", output)
		@test occursin("list of the times when a survey hit the waypoints", output)
		@test occursin("usage:", output)
		@test occursin("-Urangethreshold", output)
		@test occursin("dbg2", output)
		@test occursin("lonflip", output)
		@test occursin("rangethreshold:", output)
	end

	# TODO(schwehr): Add tests of actual usage.
end
