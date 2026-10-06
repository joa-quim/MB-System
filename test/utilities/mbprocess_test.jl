# Copyright 2019 Google Inc. All Rights Reserved.
#
# See README file for copying and redistribution conditions.

# Tests for mbprocess command line app.

include(joinpath(@__DIR__, "..", "mbtest.jl"))

const CMD = mbprog("utilities", "mbprocess")

@testset "mbprocess" begin
	@testset "NoArgs" begin
		output, code = mbrun([CMD]; stderr_to_stdout = true)
		@test code == 2
		@test occursin("Program <mbprocess> requires an input data", output)
		@test occursin("specified with the -I option.", output)
		@test occursin("The default input file is \"datalist.mb-1\".", output)
	end

	@testset "Help" begin
		output = mbcheck([CMD, "-h"]; stderr_to_stdout = true)
		@test occursin("Version", output)
		@test occursin("processing swath sonar bathymetry", output)
		@test occursin("mbedit edit save files", output)
		@test occursin("usage:", output)
		@test occursin("-S", output)
	end

	@testset "HelpVerbose2" begin
		output = mbcheck([CMD, "-h", "-V", "-V"]; stderr_to_stdout = true)
		@test occursin("Version", output)
		@test occursin("processing swath sonar bathymetry", output)
		@test occursin("mbedit edit save files", output)
		@test occursin("usage:", output)
		@test occursin("-S", output)
		# No dbg2 info.
	end

	# TODO(schwehr): Add tests of actual usage.
end
