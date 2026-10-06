# Tests for mbfnv2navlab command line app.

include(joinpath(@__DIR__, "..", "mbtest.jl"))

const CMD = mbprog("utilities", "mbfnv2navlab")

@testset "mbfnv2navlab" begin
	@testset "Help" begin
		output = mbcheck([CMD, "-H"]; stderr_to_stdout = true)
		@test occursin("Program mbfnv2navlab", output)
		@test occursin("Usage:", output)
		@test occursin("-I input.fnv", output)
	end
end
