# Copyright 2019 Google Inc. All Rights Reserved.
#
# See README file for copying and redistribution conditions.

# Tests for mbareaclean command line app.

include(joinpath(@__DIR__, "..", "mbtest.jl"))

const CMD = mbprog("utilities", "mbareaclean")

@testset "mbareaclean" begin
	@testset "NoArgs" begin
		output = mbcheck([CMD]; stderr_to_stdout = true)
		@test occursin("Unable to open data list file:", output)
		@test occursin("datalist.mb-1", output)
	end

	@testset "Help" begin
		output = mbcheck([CMD, "-h"]; stderr_to_stdout = true)
		@test occursin("identifies and flags artifacts", output)
		@test occursin("usage:", output)
		@test occursin("minbeam[/maxbeam]", output)
	end

	@testset "HelpVerbose2" begin
		output = mbcheck([CMD, "-h", "-V", "-V"]; stderr_to_stdout = true)
		@test occursin("identifies and flags artifacts", output)
		@test occursin("usage:", output)
		@test occursin("minbeam[/maxbeam]", output)
		@test occursin("dbg2", output)
		@test occursin("lonflip", output)
		@test occursin("median_filter", output)
	end

	# TODO(schwehr): Add tests of actual usage.
end
