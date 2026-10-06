# Tests for mbswath2las command line app.

include(joinpath(@__DIR__, "..", "mbtest.jl"))

const CMD = mbprog("utilities", "mbswath2las")

@testset "mbswath2las" begin
	@testset "Help" begin
		output = mbcheck([CMD, "-h"]; stderr_to_stdout = true)
		@test occursin("Program mbswath2las", output)
		@test occursin("MBswath2las exports swath bathymetry data", output)
		@test occursin("usage: mbswath2las", output)
	end

	@testset "NoDatalistFailsCleanly" begin
		# With no datalist present in the cwd, this should fail with a clear
		# message rather than crashing.
		output, code = mbrun([CMD]; stderr_to_stdout = true)
		@test code != 0
		@test occursin("Unable to open data list file", output)
	end
end
