# Copyright 2019 Google Inc. All Rights Reserved.
#
# See README file for copying and redistribution conditions.

# Tests for mbabsorption command line app.
#
# Absorption is always in dB/km.

include(joinpath(@__DIR__, "..", "mbtest.jl"))

const CMD = mbprog("utilities", "mbabsorption")
absorption(args...) = parse(Float64, strip(mbcheck([CMD, args...])))
# Python's assertAlmostEqual: equal when rounded to 7 decimal places
almost(a, b) = isapprox(a, b; atol = 5e-8)

@testset "mbabsorption" begin
	@testset "NoArgs" begin
		@test almost(absorption(), 54.446199)
	end

	@testset "Help" begin
		output = rstrip(mbcheck([CMD, "-H"]))
		@test occursin("sea water", output)
		@test occursin("soundspeed", output)
	end

	@testset "Verbose" begin
		output = rstrip(mbcheck([CMD, "-V"]))
		@test occursin("Input Parameters:", output)
		@test occursin("200.000000 kHz", output)
		@test occursin("10.000000 deg C", output)
		@test occursin("35.000000 per mil", output)
		@test occursin("0.000000 m", output)
		@test occursin("8.0", output)  # pH
	end

	@testset "SoundSpeed" begin
		@test almost(absorption("-C1500.0"), 54.047064)
	end

	@testset "Depth" begin
		@test almost(absorption("-D4000.0"), 32.836398)
	end

	@testset "Frequency" begin
		@test almost(absorption("-F31.23"), 7.354640)
	end

	@testset "Ph" begin
		@test almost(absorption("-P7.8"), 54.411284)
	end

	@testset "Salinity" begin
		@test almost(absorption("-S15.000012"), 31.259585)
	end

	@testset "Temperature" begin
		@test almost(absorption("-T0.2"), 40.702235)
	end

	# TODO(schwehr): How to handle invalid arguments?
end
