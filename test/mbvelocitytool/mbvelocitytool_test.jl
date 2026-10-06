# Tests for the mbvelocitytool interactive (Motif) GUI app.
#
# mbvelocitytool opens an X display before parsing arguments, so even -h
# needs a working DISPLAY; this test runs it under a private Xvfb server (with_xvfb in
# ../mbtest.jl), and is skipped rather than failed when no Xvfb binary is available.

include(joinpath(@__DIR__, "..", "mbtest.jl"))

const CMD = mbprog("mbvelocitytool", "mbvelocitytool")

@testset "mbvelocitytool" begin
	@testset "Help" begin
		output = with_xvfb(env -> mbcheck([CMD, "-h"]; stderr_to_stdout = true, env))
		if output === nothing
			@test_skip false	# Xvfb not found
		else
			@test occursin("Program MBVELOCITYTOOL", output)
			@test occursin("interactive water velocity profile editor", output)
		end
	end
end
