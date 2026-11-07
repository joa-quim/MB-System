# Copyright 2019 Google Inc. All Rights Reserved.
#
# See README file for copying and redistribution conditions.

# Tests for mbclean command line app.

include(joinpath(@__DIR__, "..", "mbtest.jl"))

const CMD = mbprog("utilities", "mbclean")
const MBLIST = abspath(mbprog("utilities", "mblist"))
const MBGRID = abspath(mbprog("utilities", "mbgrid"))
# synthetic swath data writer built in the build's test/utilities (test/utilities/mbcube_synthetic.cc)
const SYNTHETIC = isempty(MB_BUILD_DIR) ? abspath("mbcube_synthetic" * MB_EXE) :
                  joinpath(MB_BUILD_DIR, "test", "utilities", "mbcube_synthetic" * MB_EXE)
const HAVE_SYNTHETIC = isfile(SYNTHETIC)

const Edits = Set{Tuple{Float64,Int}}

# The (time_d, beam) of the filter edits of an edit save file (big endian time, beam, action).
function read_esf_filter_edits(path)
	data = read(path)
	io = IOBuffer(data)
	length(data) >= 10 && String(data[1:10]) == "ESFVERSION" && seek(io, 1024)
	edits = Edits()
	while position(io) + 16 <= length(data)
		time_d = ntoh(read(io, Float64))
		beam = Int(ntoh(read(io, Int32)))
		action = ntoh(read(io, Int32))
		action == 4 && push!(edits, (round(time_d; digits = 6), mod(beam, 100000000)))	# MBP_EDIT_FILTER
	end
	return edits
end

# The (time_d, beam) key and the depth of each line of an mblist -OM#z listing
function listing_entries(listing)
	entries = Tuple{Tuple{Float64,Int},Float64}[]
	for line in split(unixlines(listing), '\n'; keepempty = false)
		time_d, beam, depth = split(line)
		push!(entries, ((round(parse(Float64, time_d); digits = 6), parse(Int, beam)), parse(Float64, depth)))
	end
	return entries
end

# Flags synthetic spikes (2% of the soundings, 5 to 20 m off a flat seafloor at 100 m), returning
# the output, the detected and the falsely flagged fractions.
function run_cube_spikes(tmp, options)
	swath = joinpath(tmp, "spikes.mb71")
	isfile(swath) || mbcheck([SYNTHETIC, swath, "spikes", "100", "0"])
	datalist = joinpath(tmp, "datalist.mb-1")
	write(datalist, swath * " 71\n")
	cmd = vcat([abspath(CMD), "-I" * datalist, "--cube-spacing=2"], options)
	output, listing = cd(tmp) do
		output = unixlines(mbcheck(cmd; stderr_to_stdout = true))
		return output, mbcheck([MBLIST, "-I" * swath, "-F71", "-MA", "-OM#z"])
	end
	println("\nOutput from ", join(cmd, ' '), ":\n--------------------------")
	println(output)
	spikes = Edits()
	good = Edits()
	for (key, depth) in listing_entries(listing)
		push!(abs(depth - 100.0) > 1.0 ? spikes : good, key)
	end
	flagged = read_esf_filter_edits(swath * ".esf")
	@test length(spikes) > 1000
	return output, length(intersect(flagged, spikes)) / length(spikes), length(intersect(flagged, good)) / length(good)
end

# Flags a flat seafloor at 100 m whose nadir soundings (within 7.5 degrees) are 1 m shallower (20%)
# or deeper (5%), with 2% of the other soundings 1 m off either way. Returns the output and the
# flagged fractions of the shallow and deep nadir soundings, of the other offset soundings and of
# the good soundings.
function run_cube_nadir(tmp, options)
	swath = joinpath(tmp, "nadir.mb71")
	isfile(swath * ".esf") && rm(swath * ".esf")
	isfile(swath) || mbcheck([SYNTHETIC, swath, "nadir", "100", "0"])
	cmd = vcat([abspath(CMD), "-I" * swath, "-V", "--cube-spacing=2", "--cube-model=constant",
	            "--cube-sigma-v=0.05", "--cube-sigma-h=0.5"], options)
	output, listing = cd(tmp) do
		output = unixlines(mbcheck(cmd; stderr_to_stdout = true))
		return output, mbcheck([MBLIST, "-I" * swath, "-F71", "-MA", "-OM#z"])
	end
	groups = Dict(name => Edits() for name in ("shallow", "deep", "other", "good"))
	for (key, depth) in listing_entries(listing)
		nadir = abs(-60.0 + 1.2 * key[2]) < 7.5	# 101 beams over +/-60 degrees
		offset = depth - 100.0
		if abs(offset) < 0.5
			push!(groups["good"], key)
		elseif nadir
			push!(groups[offset < 0.0 ? "shallow" : "deep"], key)
		else
			push!(groups["other"], key)
		end
	end
	for name in ("shallow", "deep", "other")
		@test length(groups[name]) > 100
	end
	flagged = isfile(swath * ".esf") ? read_esf_filter_edits(swath * ".esf") : Edits()
	return output, Dict(name => length(intersect(flagged, keys)) / length(keys) for (name, keys) in groups)
end

@testset "mbclean" begin
	@testset "NoArgs" begin
		output, code = mbrun([CMD]; stderr_to_stdout = true)
		@test code == 2
		@test occursin("Unable to open data list file:", output)
		@test occursin("datalist.mb-1", output)
	end

	@testset "Help" begin
		output = mbcheck([CMD, "-h"]; stderr_to_stdout = true)
		@test occursin("identifies and flags artifacts", output)
		@test occursin("-Sspike_slope", output)
	end

	@testset "HelpVerbose2" begin
		output = mbcheck([CMD, "-h", "-V", "-V"]; stderr_to_stdout = true)
		@test occursin("identifies and flags artifacts", output)
		@test occursin("-Sspike_slope", output)
		@test occursin("lonflip", output)
	end

	@testset "HelpCube" begin
		output = mbcheck([CMD, "-h"]; stderr_to_stdout = true)
		@test occursin("--cube-spacing=dx[/dy]", output)
		@test occursin("--cube-flag=hypothesis|outlier|both", output)
		@test occursin("--cube-iho-edition=4|5|6", output)
		@test occursin("--cube-prediction=median[/factor]|gridfile", output)
		@test occursin("--cube-prediction-topography", output)
		@test occursin("--cube-flag-nadir=angle/threshold[/shallow|both]", output)
	end

	@testset "CubeCannotBeCombined" begin
		output, code = mbrun([CMD, "--cube-spacing=2", "-C3"]; stderr_to_stdout = true)
		@test code == 9	# MB_ERROR_BAD_USAGE
		@test occursin("cannot be combined", output)
	end

	@testset "CubeInvalidOptions" begin
		for option in ("--cube-order=4", "--cube-flag=all", "--cube-spacing=2/x", "--cube-flag-nadir=10",
		               "--cube-flag-nadir=10/1.5/deep", "--cube-flag-nadir=10/1.5x", "--cube-flag-nadir=10/1.5/")
			output, code = mbrun([CMD, "--cube-spacing=2", option]; stderr_to_stdout = true)
			@test code == 9
			@test occursin("Invalid CUBE option: " * option, output)
		end
	end

	@testset "CubeBayesFactorTooLarge" begin
		# from exp(h^2 / 2) on every sounding is an outlier (threshold below zero)
		output, code = mbrun([CMD, "--cube-spacing=2", "--cube-est-offset=1", "--cube-bayes-factor=2"];
		                     stderr_to_stdout = true)
		@test code == 12	# MB_ERROR_BAD_PARAMETER
		@test occursin("(--cube-bayes-factor) must be below exp(h^2 / 2)", output)
	end

	@testset "CubeNadirBadParameters" begin
		for (options, message) in ((["--cube-flag-nadir=0/1.5"], "needs an angle between 0 and 90 degrees"),
		                           (["--cube-flag-nadir=10/0"], "needs an angle between 0 and 90 degrees"),
		                           (["--cube-flag-nadir=10/1.5", "--cube-flag=hypothesis"], "needs the outlier criterion"))
			output, code = mbrun(vcat([CMD, "--cube-spacing=2"], options); stderr_to_stdout = true)
			@test code == 12	# MB_ERROR_BAD_PARAMETER
			@test occursin(message, output)
		end
	end

	@testset "CubePredictionTopographyNeedsGrid" begin
		output, code = mbrun([CMD, "--cube-spacing=2", "--cube-prediction-topography"]; stderr_to_stdout = true)
		@test code == 12
		@test occursin("needs a predicted surface grid", output)
	end

	@testset "CubeNadirShallow" begin
		if !HAVE_SYNTHETIC
			@test_skip "mbcube_synthetic not built"
		else
			mktempdir() do tmp
				output, flagged = run_cube_nadir(tmp, ["--cube-flag-nadir=8/5", "--cube-flag-threshold=50"])
				@test occursin("Nadir sector:           beam angle < 8.0 deg: soundings more than 5.000 normalized errors " *
				               "shallower than the selected hypothesis", output)
				# at a few nodes the shallow soundings outnumber the others and form the selected
				# hypothesis, so not all of them are flagged
				@test flagged["shallow"] > 0.9
				@test flagged["deep"] < 0.01
				@test flagged["other"] < 0.01
				@test flagged["good"] < 0.001
				# -V lists the flagged soundings with their beam angle (here below 8 degrees)
				angles = [parse(Float64, split(line)[end]) for line in split(output, '\n') if startswith(line, "c: ")]
				@test length(angles) > 100
				@test !isempty(angles) && maximum(angles) < 8.0
			end
		end
	end

	@testset "CubeNadirBoth" begin
		if !HAVE_SYNTHETIC
			@test_skip "mbcube_synthetic not built"
		else
			mktempdir() do tmp
				output, flagged = run_cube_nadir(tmp, ["--cube-flag-nadir=8/5/both", "--cube-flag-threshold=50"])
				@test occursin("normalized errors from the selected hypothesis", output)
				@test flagged["shallow"] > 0.9
				@test flagged["deep"] > 0.95
				@test flagged["other"] < 0.01
				# good soundings at the nodes where the shallow ones form the selected hypothesis are
				# 1 m deeper than it
				@test flagged["good"] < 0.005
			end
		end
	end

	@testset "CubeNadirOff" begin
		if !HAVE_SYNTHETIC
			@test_skip "mbcube_synthetic not built"
		else
			mktempdir() do tmp
				# without the option one threshold applies to all soundings, and the -V lines keep
				# their columns
				output, flagged = run_cube_nadir(tmp, ["--cube-flag-threshold=5"])
				@test !occursin("Nadir sector", output)
				for name in ("shallow", "deep", "other")
					@test flagged[name] > 0.9
				end
				@test flagged["good"] < 0.005
				@test all(length(split(line)) == 11 for line in split(output, '\n') if startswith(line, "c: "))
			end
		end
	end

	@testset "CubeFlagsSpikes" begin
		if !HAVE_SYNTHETIC
			@test_skip "mbcube_synthetic not built"
		else
			mktempdir() do tmp
				output, detected, falsely = run_cube_spikes(tmp, ["--cube-order=special"])
				@test occursin("CUBE flagging:", output)
				@test occursin("IHO S-44 Ed. 6 Special Order", output)
				@test occursin("2.501 normalized errors", output)
				@test detected > 0.98
				@test falsely < 0.001
				par = joinpath(tmp, "spikes.mb71.par")
				@test occursin("EDITSAVEFILE", read(par, String))
				par_mtime = mtime(par)
				# a second run finds the flagged soundings already flagged, and leaves the parameter
				# file as it is
				output, detected_again, falsely_again = run_cube_spikes(tmp, ["--cube-order=special"])
				@test occursin("\n0 total beams flagged", output)
				@test par_mtime == mtime(par)
				@test detected == detected_again
				@test falsely == falsely_again
			end
		end
	end

	@testset "CubeFlagsSpikesWithMedianPrediction" begin
		if !HAVE_SYNTHETIC
			@test_skip "mbcube_synthetic not built"
		else
			mktempdir() do tmp
				output, detected, falsely = run_cube_spikes(tmp,
					["--cube-model=constant", "--cube-sigma-v=0.05", "--cube-sigma-h=0.5",
					 "--cube-prediction=median/2", "--cube-extractor=predicted"])
				@test occursin("data points used for the median predicted surface", output)
				@test detected > 0.98
				@test falsely < 0.002
			end
		end
	end

	@testset "CubeHypothesisOnly" begin
		if !HAVE_SYNTHETIC
			@test_skip "mbcube_synthetic not built"
		else
			mktempdir() do tmp
				output, detected, falsely = run_cube_spikes(tmp, ["--cube-order=special", "--cube-flag=hypothesis"])
				@test occursin("\n0 total soundings exceeding the outlier threshold", output)
				@test detected < 0.5
				@test falsely < 0.001
			end
		end
	end

	@testset "CubeSingleFile" begin
		if !HAVE_SYNTHETIC
			@test_skip "mbcube_synthetic not built"
		else
			mktempdir() do tmp
				swath = joinpath(tmp, "spikes.mb71")
				mbcheck([SYNTHETIC, swath, "spikes", "100", "0"])
				cmd = [abspath(CMD), "-I" * swath, "--cube-spacing=2", "--cube-order=special"]
				output = cd(() -> mbcheck(cmd; stderr_to_stdout = true), tmp)
				@test occursin("Grid dimensions:        234 x 151", output)
				@test occursin("1 total swath data files processed", output)
				@test length(read_esf_filter_edits(swath * ".esf")) > 1200
			end
		end
	end

	@testset "CubePredictionGridBlunders" begin
		if !HAVE_SYNTHETIC
			@test_skip "mbcube_synthetic not built"
		else
			mktempdir() do tmp
				# a predicted surface 30 m too shallow: the blunder test rejects the soundings deeper
				# than 70 m + max(10 m, 25%, 3 sigma), so only the shallow spikes reach the nodes and the
				# seafloor soundings compared with them are blunders
				shallow = joinpath(tmp, "shallow.mb71")
				mbcheck([SYNTHETIC, shallow, "flat", "70", "0"])
				shallow_list = joinpath(tmp, "shallow.mb-1")
				write(shallow_list, shallow * " 71\n")
				swath = joinpath(tmp, "spikes.mb71")
				output = cd(tmp) do
					mbcheck([MBGRID, "-I" * shallow_list, "-O" * joinpath(tmp, "pred"), "-A1", "-F2", "-E4/4/meters"];
					        stderr_to_stdout = true)
					mbcheck([SYNTHETIC, swath, "spikes", "100", "0"])
					cmd = [abspath(CMD), "-I" * swath, "--cube-spacing=2", "--cube-order=special",
					       "--cube-prediction=" * joinpath(tmp, "pred.grd")]
					return unixlines(mbcheck(cmd; stderr_to_stdout = true))
				end
				@test occursin("Predicted surface set at", output)
				blunders = [parse(Int, split(line)[1]) for line in split(output, '\n')
				            if endswith(line, "total soundings rejected by the blunder test")]
				@test length(blunders) == 1
				@test !isempty(blunders) && blunders[1] > 0
			end
		end
	end

	@testset "CubePredictionGridSign" begin
		if !HAVE_SYNTHETIC
			@test_skip "mbcube_synthetic not built"
		else
			mktempdir() do tmp
				# the 100 m seafloor of the spike data as a grid of depths (-A1) and as a grid of
				# heights (-A2)
				flat = joinpath(tmp, "flat.mb71")
				mbcheck([SYNTHETIC, flat, "flat", "100", "0"])
				flat_list = joinpath(tmp, "flat.mb-1")
				write(flat_list, flat * " 71\n")
				edits = Dict{String,Edits}()
				cd(tmp) do
					for (name, datatype) in (("depth", "-A1"), ("height", "-A2"))
						mbcheck([MBGRID, "-I" * flat_list, "-O" * joinpath(tmp, name), datatype, "-F2", "-E4/4/meters"];
						        stderr_to_stdout = true)
					end
					for (name, options) in (("depth", String[]), ("height", ["--cube-prediction-topography"]))
						swath = joinpath(tmp, "spikes_" * name * ".mb71")
						mbcheck([SYNTHETIC, swath, "spikes", "100", "0"])
						cmd = [abspath(CMD), "-I" * swath, "--cube-spacing=2", "--cube-order=special",
						       "--cube-prediction=" * joinpath(tmp, name * ".grd")]
						if name == "height"
							# read as depths, the heights are opposite to the soundings: stopped after the
							# first read, before anything is flagged
							output, code = mbrun(cmd; stderr_to_stdout = true)
							@test code == 12
							@test occursin("has the opposite sign to", output)
							@test !isfile(swath * ".esf")
						end
						output = mbcheck(vcat(cmd, options); stderr_to_stdout = true)
						if name == "height"
							@test occursin("(heights, positive up)", output)
						end
						edits[name] = read_esf_filter_edits(swath * ".esf")
					end
				end
				# the -A1 and -A2 median grids differ by up to a few decimeters (mbgrid takes one of the
				# two middle values of an even count)
				@test length(edits["depth"]) > 1200
				@test length(intersect(edits["depth"], edits["height"])) > 0.98 * length(edits["depth"])
				@test abs(length(edits["height"]) - length(edits["depth"])) < 0.02 * length(edits["depth"])
			end
		end
	end
end
