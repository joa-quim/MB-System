# Copyright 2019 Google Inc. All Rights Reserved.
#
# See README file for copying and redistribution conditions.

# Tests for mbhsdump command line app.

include(joinpath(@__DIR__, "..", "mbtest.jl"))

const CMD = mbprog("deprecated", "mbhsdump")

@testset "mbhsdump" begin
	# Hangs: testNoArgs

	@testset "Help" begin
		output = mbcheck([CMD, "-h"]; stderr_to_stdout = true)
		@test occursin("Version", output)
		@test occursin("information contained in data records", output)
		@test occursin("usage:", output)
		@test occursin("-Okind", output)
	end

	@testset "HelpVerbose2" begin
		output = mbcheck([CMD, "-h", "-V", "-V"]; stderr_to_stdout = true)
		@test occursin("Version", output)
		@test occursin("information contained in data records", output)
		@test occursin("usage:", output)
		@test occursin("-Okind", output)
		@test occursin("dbg2", output)
		@test occursin("lonflip", output)
		@test occursin("mb_data_velocity_profile_list:", output)
	end

	# TODO(schwehr): Add tests of actual usage.
end
