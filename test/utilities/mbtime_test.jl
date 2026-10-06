# Copyright 2019 Google Inc. All Rights Reserved.
#
# See README file for copying and redistribution conditions.

# Tests for mbtime command line app.

include(joinpath(@__DIR__, "..", "mbtest.jl"))

const CMD = mbprog("utilities", "mbtime")
out(args...) = strip(mbcheck([CMD, args...]; stderr_to_stdout = true))

@testset "mbtime" begin
	@testset "NoArgs" begin
		@test out() == "1970/01/01/00/00/00.000000"
	end

	@testset "Help" begin
		output = mbcheck([CMD, "-h"]; stderr_to_stdout = true)
		@test occursin("Version", output)
		@test occursin("converts time values between epoch seconds", output)
		@test occursin("usage:", output)
		@test occursin("-Tyear/month/day/hour/minute/second", output)
	end

	@testset "HelpVerbose2" begin
		output = mbcheck([CMD, "-h", "-V", "-V"]; stderr_to_stdout = true)
		@test occursin("Version", output)
		@test occursin("converts time values between epoch seconds", output)
		@test occursin("usage:", output)
		@test occursin("-Tyear/month/day/hour/minute/second", output)
		@test occursin("dbg2", output)
		@test occursin("time_d:", output)
	end

	@testset "Sec0" begin
		@test out("-M", "0") == "1970/01/01/00/00/00.000000"
	end

	@testset "Sec1" begin
		@test out("-M", "1.2345678") == "1970/01/01/00/00/01.234567"
	end

	@testset "SecInManPage" begin
		@test out("-M1212777434.0") == "2008/06/06/18/37/14.000000"
	end

	# Negative values do not work for input seconds.  e.g. -M -2000.1234

	@testset "DateEpoch" begin
		@test out("-T", "1970/01/01/00/00/00.000000") == "0.000000"
	end

	@testset "DateInManPage" begin
		@test out("-T2008/06/06/18/37/14.0") == "1212777434.000000"
	end

	@testset "DateNegative" begin
		@test out("-T", "1969/12/31/00/00/00.000000") == "-86400.000000"
	end

	# https://en.wikipedia.org/wiki/Year_2038_problem

	@testset "Year2028Max32" begin
		@test out("-M2147483647") == "2038/01/19/03/14/07.000000"
	end

	@testset "Year2028DoNotOverflow" begin
		@test out("-M2147483648") == "2038/01/19/03/14/08.000000"
	end

	@testset "Year2028Max32Date" begin
		@test out("-T2038/01/19/03/14/07.000000") == "2147483647.000000"
	end

	@testset "Year2028DoNotOverflowDate" begin
		@test out("-T2038/01/19/03/14/08.00000") == "2147483648.000000"
	end
end
