# Smoke test for the mbmesh command line app itself (as opposed to the
# algorithm-level tests alongside this file, e.g. test_settings.cpp).

include(joinpath(@__DIR__, "..", "..", "..", "test", "mbtest.jl"))

const CMD = mbprog("mbmesh", "mbmesh")

@testset "mbmesh" begin
	@testset "Help" begin
		output = mbcheck([CMD, "-h"]; stderr_to_stdout = true)
		@test occursin("mbmesh generates 3D meshes from swath sonar bathymetry", output)
		@test occursin("usage: mbmesh", output)
	end

	@testset "NoInputFailsCleanly" begin
		output, code = mbrun([CMD]; stderr_to_stdout = true)
		@test code != 0
		@test occursin("input datalist path is empty", output)
	end
end
