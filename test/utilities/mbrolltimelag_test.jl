# Copyright 2019 Google Inc. All Rights Reserved.
#
# See README file for copying and redistribution conditions.

# Tests for mbrolltimelag command line app.

include(joinpath(@__DIR__, "..", "mbtest.jl"))

const CMD = mbprog("utilities", "mbrolltimelag")

@testset "mbrolltimelag" begin
	@testset "NoArgs" begin
		output, code = mbrun([CMD]; stderr_to_stdout = true)
		@test code == 2
		@test occursin("Unable to open data list file:", output)
		@test occursin("datalist.mb-1", output)
		@test occursin("Running mbnavlist", output)
		@test occursin("0 roll data read from datalist.mb-1", output)
	end

	@testset "Help" begin
		output = mbcheck([CMD, "-h"]; stderr_to_stdout = true)
		@test occursin("Version", output)
		@test occursin("extracts the roll time series", output)
		@test occursin("usage:", output)
		@test occursin("-Tnlag/lagmin/lagmax", output)
	end

	@testset "HelpVerbose2" begin
		output = mbcheck([CMD, "-h", "-V", "-V"]; stderr_to_stdout = true)
		@test occursin("Version", output)
		@test occursin("extracts the roll time series", output)
		@test occursin("usage:", output)
		@test occursin("-Tnlag/lagmin/lagmax", output)
		@test occursin("dbg2", output)
		@test occursin("kind:", output)
	end

	# TODO(schwehr): Add tests of actual usage.
end
