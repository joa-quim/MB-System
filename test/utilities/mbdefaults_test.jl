# Copyright 2019 Google Inc. All Rights Reserved.
#
# See README file for copying and redistribution conditions.

# Tests for mbdefaults command line app.

include(joinpath(@__DIR__, "..", "mbtest.jl"))

const CMD = mbprog("utilities", "mbdefaults")

@testset "mbdefaults" begin
	@testset "NoArgs" begin
		output = mbcheck([CMD]; stderr_to_stdout = true)
		@test occursin("Current MBIO Default Control Parameters:", output)
		@test occursin("lonflip:", output)
		@test occursin("mbview slope magnitude:", output)
	end

	@testset "Help" begin
		output = mbcheck([CMD, "-h"]; stderr_to_stdout = true)
		@test occursin("Version", output)
		@test occursin("sets and retrieves", output)
		@test occursin("parameters", output)
		@test occursin("-Wmbproject", output)
	end

	@testset "HelpVerbose2" begin
		output = mbcheck([CMD, "-h", "-V", "-V"]; stderr_to_stdout = true)
		@test occursin("Version", output)
		@test occursin("sets and retrieves", output)
		@test occursin("parameters", output)
		@test occursin("-Wmbproject", output)
		@test occursin("dbg2", output)
		@test occursin("lonflip", output)
		@test occursin("illuminate_azimuth:", output)
	end

	# TODO(schwehr): Add tests of actual usage.
end
