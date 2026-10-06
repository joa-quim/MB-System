# Tests for mbmakedatalist command line app.

include(joinpath(@__DIR__, "..", "mbtest.jl"))

const CMD = mbprog("utilities", "mbmakedatalist")

@testset "mbmakedatalist" begin
	@testset "Help" begin
		# Note: mbmakedatalist scans the current directory for swath files by
		# default, so this test deliberately only exercises -h (no side effects)
		# rather than a bare invocation.
		output = mbcheck([CMD, "-h"]; stderr_to_stdout = true)
		@test occursin("mbmakedatalist:", output)
		@test occursin("Macro to generate an MB-System datalist file", output)
	end
end
