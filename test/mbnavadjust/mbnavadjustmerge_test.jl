# Tests for mbnavadjustmerge command line app.

include(joinpath(@__DIR__, "..", "mbtest.jl"))

const CMD = mbprog("mbnavadjust", "mbnavadjustmerge")

@testset "mbnavadjustmerge" begin
	@testset "Help" begin
		output = mbcheck([CMD, "--help"]; stderr_to_stdout = true)
		@test occursin("Program mbnavadjustmerge", output)
		@test occursin("merges two existing mbnavadjust projects", output)
		@test occursin("usage: mbnavadjustmerge --input=project_path", output)
	end
end
