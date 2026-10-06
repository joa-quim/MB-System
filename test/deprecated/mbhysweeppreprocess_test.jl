# Copyright 2019 Google Inc. All Rights Reserved.
#
# See README file for copying and redistribution conditions.

# Tests for mbhysweeppreprocess command line app.

include(joinpath(@__DIR__, "..", "mbtest.jl"))

const CMD = mbprog("deprecated", "mbhysweeppreprocess")

@testset "mbhysweeppreprocess" begin
	@testset "NoArgs" begin
		output, code = mbrun([CMD]; stderr_to_stdout = true)
		@test code == 2
		@test occursin("Data available for merging:", output)
		@test occursin("sonardepth", output)
		@test occursin("Unable to open data list file:", output)
		@test occursin("datalist.mb-1", output)
	end

	@testset "Help" begin
		output = mbcheck([CMD, "-h"]; stderr_to_stdout = true)
		@test occursin("Version", output)
		@test occursin("reads a Hysweep HSX format file", output)
		@test occursin("usage:", output)
		@test occursin("-Ttimelag", output)
	end

	@testset "HelpVerbose2" begin
		output = mbcheck([CMD, "-h", "-V", "-V"]; stderr_to_stdout = true)
		@test occursin("Version", output)
		@test occursin("reads a Hysweep HSX format file", output)
		@test occursin("usage:", output)
		@test occursin("-Ttimelag", output)
		@test occursin("dbg2", output)
		@test occursin("lonflip", output)
		@test occursin("timelagmode:", output)
	end

	# TODO(schwehr): Add tests of actual usage.
end
