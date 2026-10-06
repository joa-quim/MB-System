# Tests for mbmapscale command line app.

include(joinpath(@__DIR__, "..", "mbtest.jl"))

const CMD = mbprog("utilities", "mbmapscale")

@testset "mbmapscale" begin
	@testset "Help" begin
		output = mbcheck([CMD, "-h"]; stderr_to_stdout = true)
		@test occursin("mbmapscale outputs the scaling", output)
		@test occursin("--help", output)
		@test occursin("--latitude=latitude", output)
	end

	@testset "NoArgsUsesDefaultLatitude" begin
		output = mbcheck([CMD]; stderr_to_stdout = true)
		@test occursin("Meters per degree longitude:", output)
		@test occursin("Meters per degree latitude:", output)
	end
end
