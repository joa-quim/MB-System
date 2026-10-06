# Copyright 2019 Google Inc. All Rights Reserved.
#
# See README file for copying and redistribution conditions.

# Tests for mb7kpreprocess command line app.

include(joinpath(@__DIR__, "..", "mbtest.jl"))

const CMD = mbprog("deprecated", "mb7kpreprocess")

@testset "mb7kpreprocess" begin
	@testset "NoArgs" begin
		output, code = mbrun([CMD]; stderr_to_stdout = true)
		@test code == 2
		@test occursin("Unable to open data list file:", output)
		@test occursin("datalist.mb-1", output)
		@test occursin("Ancillary data sources", output)
	end

	@testset "Help" begin
		output = mbcheck([CMD, "-h"]; stderr_to_stdout = true)
		@test occursin("mb7kpreprocess reads a Reson 7k", output)
		@test occursin("usage:", output)
		@test occursin("-Crollbias", output)
	end

	@testset "HelpVerbose2" begin
		output = mbcheck([CMD, "-h", "-V", "-V"]; stderr_to_stdout = true)
		@test occursin("mb7kpreprocess reads a Reson 7k", output)
		@test occursin("usage:", output)
		@test occursin("-Crollbias", output)
		@test occursin("dbg2", output)
		@test occursin("lonflip", output)
		@test occursin("rollpitch_offset_pitch", output)
	end

	# TODO(schwehr): Add tests of actual usage.
end
