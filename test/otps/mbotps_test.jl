# Tests for mbotps command line app.

include(joinpath(@__DIR__, "..", "mbtest.jl"))

const CMD = mbprog("otps", "mbotps")

@testset "mbotps" begin
	@testset "Help" begin
		output = mbcheck([CMD, "-h"]; stderr_to_stdout = true)
		@test occursin("Program mbotps", output)
		@test occursin("MBotps predicts tides", output)
		@test occursin("usage: mbotps", output)
		@test occursin("--input=datalist", output)
	end
end
