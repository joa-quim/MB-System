# Copyright 2019 Google Inc. All Rights Reserved.
#
# See README file for copying and redistribution conditions.

# Tests for mbgetesf command line app.

include(joinpath(@__DIR__, "..", "mbtest.jl"))

const CMD = mbprog("utilities", "mbgetesf")

@testset "mbgetesf" begin
	@testset "NoArgs" begin
		output, code = mbrun([CMD]; stderr_to_stdout = true)
		@test code == 3
		@test occursin("MBIO Error returned from function", output)
		@test occursin("Illegal format identifier", output)
		@test occursin("Multibeam File <stdin> not initialized", output)
		@test occursin("Program <mbgetesf> Terminated", output)
	end

	@testset "Help" begin
		output = mbcheck([CMD, "-h"]; stderr_to_stdout = true)
		@test occursin("Version", output)
		@test occursin("reads a multibeam data file and writes", output)
		@test occursin("used to apply the edit events to another file", output)
		@test occursin("usage:", output)
		@test occursin("-Oesffile", output)
	end

	@testset "HelpVerbose2" begin
		output = mbcheck([CMD, "-h", "-V", "-V"]; stderr_to_stdout = true)
		@test occursin("Version", output)
		@test occursin("reads a multibeam data file and writes", output)
		@test occursin("used to apply the edit events to another file", output)
		@test occursin("usage:", output)
		@test occursin("-Oesffile", output)
		@test occursin("dbg2", output)
		@test occursin("lonflip", output)
		@test occursin("kluge:", output)
	end

	# TODO(schwehr): Add tests of actual usage.
end
