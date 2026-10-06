# Tests for the mbedit interactive (Motif) GUI app.
#
# mbedit opens an X display before parsing arguments, so even -h needs a
# working DISPLAY; this test runs it under a private Xvfb server (with_xvfb in
# ../mbtest.jl), and is skipped rather than failed when no Xvfb binary is available.

include(joinpath(@__DIR__, "..", "mbtest.jl"))

const CMD = mbprog("mbedit", "mbedit")

@testset "mbedit" begin
	@testset "Help" begin
		output = with_xvfb(env -> mbcheck([CMD, "-h"]; stderr_to_stdout = true, env))
		if output === nothing
			@test_skip false	# Xvfb not found
		else
			@test occursin("Program MBedit", output)
			@test occursin("interactive editor used to identify and flag", output)
		end
	end
end
