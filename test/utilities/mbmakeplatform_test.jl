# Copyright 2019 Google Inc. All Rights Reserved.
#
# See README file for copying and redistribution conditions.

# Tests for mbmakeplatform command line app.

include(joinpath(@__DIR__, "..", "mbtest.jl"))

const CMD = mbprog("utilities", "mbmakeplatform")

@testset "mbmakeplatform" begin
	@testset "NoArgs" begin
		@test strip(mbcheck([CMD]; stderr_to_stdout = true)) == ""
	end

	@testset "Help" begin
		output = mbcheck([CMD, "--help"]; stderr_to_stdout = true)
		@test occursin("creates or modifies an MB-System platform file", output)
		@test occursin("usage:", output)
		@test occursin("--input=plffile", output)
		@test occursin("--modify-time-latency-model=file", output)
	end

	@testset "HelpVerbose2" begin
		output = mbcheck([CMD, "--help", "-verbose", "--verbose"]; stderr_to_stdout = true)
		@test occursin("creates or modifies an MB-System platform file", output)
		@test occursin("usage:", output)
		@test occursin("--input=plffile", output)
		@test occursin("--modify-time-latency-model=file", output)
		# No extra dbg2 info.
	end

	# TODO(schwehr): Add tests of actual usage.
end
