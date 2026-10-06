# Tests for mbgrd2gltf command line app.

include(joinpath(@__DIR__, "..", "mbtest.jl"))

const CMD = mbprog("mbgrd2gltf", "mbgrd2gltf")

@testset "mbgrd2gltf" begin
	@testset "Help" begin
		output = mbcheck([CMD, "-h"]; stderr_to_stdout = true)
		@test occursin("MBgrd2gltf", output)
		@test occursin("converts a GMT GRD format bathymetry grid file", output)
		@test occursin("usage: mbgrd2gltf", output)
	end
end
