# Copyright 2019 Google Inc. All Rights Reserved.
#
# See README file for copying and redistribution conditions.

# Tests for mbgrid command line app.

include(joinpath(@__DIR__, "..", "mbtest.jl"))

const CMD = mbprog("utilities", "mbgrid")
# synthetic swath data writer built in the build's test/utilities (test/utilities/mbcube_synthetic.cc)
const SYNTHETIC = isempty(MB_BUILD_DIR) ? abspath("mbcube_synthetic" * MB_EXE) :
                  joinpath(MB_BUILD_DIR, "test", "utilities", "mbcube_synthetic" * MB_EXE)

# Reads an mbgrid -G1 ascii grid, returning the list of values.
function read_ascii_grid(path)
	lines = split(read(path, String), '\n')
	nx, ny = (parse(Int, v) for v in split(lines[3]))
	values = [parse(Float64, v) for line in lines[5:end] for v in split(line)]
	@assert length(values) == nx * ny
	return values
end

# Absolute errors of the grid nodes that have data, and the node count.
function depth_errors(path, truth)
	values = read_ascii_grid(path)
	errors = sort([abs(v - truth) for v in values if v < 99990.0])
	return errors, length(values)
end

# 99th percentile of the sorted node errors, indexed as Python's errors[int(0.99 * len(errors))]
p99(errors) = errors[floor(Int, 0.99 * length(errors)) + 1]

# Writes synthetic swath data and grids it as -G1 ascii grids.
function run_cube(tmp, seafloor, depth, sensordepth, root, options)
	swath = joinpath(tmp, root * ".mb71")
	datalist = joinpath(tmp, root * "_datalist.mb-1")
	mbcheck([SYNTHETIC, swath, seafloor, string(depth), string(sensordepth)])
	write(datalist, swath * " 71\n")
	cmd = vcat([abspath(CMD), "-I" * datalist, "-O" * joinpath(tmp, root), "-A1", "-G1"], options)
	output = cd(() -> mbcheck(cmd; stderr_to_stdout = true), tmp)
	println("\nOutput from ", join(cmd, ' '), ":\n--------------------------")
	println(output)
	return unixlines(output)
end

const BOUNDS = ["-R-124.51/-124.49/40.79/40.81", "-E10/10/meters"]

@testset "mbgrid" begin
	@testset "NoArgs" begin
		output = mbcheck([CMD]; stderr_to_stdout = true)
		@test occursin("Unable to open data list file:", output)
		@test occursin("datalist.mb-1", output)
	end

	@testset "Help" begin
		output = mbcheck([CMD, "-h"]; stderr_to_stdout = true)
		@test occursin("Version", output)
		@test occursin("used to grid bathymetry, amplitude", output)
		@test occursin("curvature algorithm", output)
		@test occursin("usage:", output)
		@test occursin("-Xextend", output)
		@test occursin("CUBE", output)
		@test occursin("--cube-order=exclusive|special|1a|1b|2", output)
		@test occursin("--cube-depth-reference=sonar|surface", output)
		@test occursin("--cube-model-c=fraction", output)
		@test occursin("--cube-prediction=median[/factor]|gridfile", output)
	end

	@testset "HelpVerbose2" begin
		output = mbcheck([CMD, "-h", "-V", "-V"]; stderr_to_stdout = true)
		@test occursin("Version", output)
		@test occursin("used to grid bathymetry, amplitude", output)
		@test occursin("curvature algorithm", output)
		@test occursin("usage:", output)
		@test occursin("-Xextend", output)
		@test occursin("dbg2", output)
		@test occursin("lonflip", output)
		@test occursin("minormax_weighted_mean_threshold:", output)
	end

	@testset "CubeInvalidOption" begin
		output, code = mbrun([CMD, "-F9", "--cube-order=4"]; stderr_to_stdout = true)
		@test code != 0
		@test occursin("Invalid CUBE option: --cube-order=4", output)
	end

	@testset "CubeOrderOfOtherEdition" begin
		# Order 3 exists only in edition 4 (and the CCOM table), Order 1a only in editions 5 and 6,
		# Exclusive Order only in edition 6
		for (options, message) in ((["--cube-order=3"], "IHO S-44 edition 6 has no order 3"),
		                           (["--cube-iho-edition=4", "--cube-order=1a"], "IHO S-44 edition 4 has no order 1a"),
		                           (["--cube-iho-edition=5", "--cube-order=exclusive"], "IHO S-44 edition 5 has no order exclusive"),
		                           (["--cube-iho-ccom", "--cube-order=1b"], "errmod_iho table (--cube-iho-ccom) has no order 1b"),
		                           (["--cube-iho-ccom", "--cube-iho-edition=4"], "cannot be combined"),
		                           (["--cube-iho-edition=3"], "Invalid CUBE option: --cube-iho-edition=3"))
			output, code = mbrun(vcat([CMD, "-F9"], options, BOUNDS); stderr_to_stdout = true)
			@test code != 0
			@test occursin(message, output)
		end
	end

	@testset "CubeIhoEdition" begin
		# the selected edition and order are reported before the data are read (run in a scratch
		# directory: mbgrid writes the list of the files used, grid.mb-1, in the current directory)
		cases = ((["--cube-iho-edition=4", "--cube-order=3"],
		          "IHO S-44 Ed. 4 Order 3 (TVU a 1.000 m, b 0.0230; THU 150.0 m + 5% of depth)"),
		         (["--cube-iho-edition=5", "--cube-order=2"],
		          "IHO S-44 Ed. 5 Order 2 (TVU a 1.000 m, b 0.0230; THU 20.0 m + 10% of depth)"),
		         (["--cube-order=2", "--cube-iho-edition=4"],
		          "IHO S-44 Ed. 4 Order 2 (TVU a 1.000 m, b 0.0230; THU 20.0 m + 5% of depth)"),
		         (["--cube-iho-ccom", "--cube-order=special"],
		          "CCOM errmod_iho Special Order (TVU a 0.200 m, b 0.0075; THU 2.0 m + 0% of depth)"))
		mktempdir() do tmp
			cd(tmp) do
				for (options, message) in cases
					output, _ = mbrun(vcat([abspath(CMD), "-F9"], options, BOUNDS); stderr_to_stdout = true)
					@test occursin("Sounding uncertainty:   " * message, output)
				end
			end
		end
	end

	@testset "CubeRejectsAmplitude" begin
		output, code = mbrun(vcat([CMD, "-F9", "-A3"], BOUNDS); stderr_to_stdout = true)
		@test code != 0
		@test occursin("only available for bathymetry and topography", output)
	end

	@testset "CubeRejectsTimeDifference" begin
		output, code = mbrun(vcat([CMD, "-F9", "-U10"], BOUNDS); stderr_to_stdout = true)
		@test code != 0
		@test occursin("cannot be used with CUBE gridding", output)
	end

	@testset "CubeFlatSeafloor" begin
		if !isfile(SYNTHETIC)
			@test_skip "mbcube_synthetic not built"
		else
			mktempdir() do tmp
				output = run_cube(tmp, "flat", 100, 0, "flat", ["-F9", "-E2/2/meters", "-M"])
				@test occursin("CUBE (Combined Uncertainty and Bathymetric Estimator)", output)
				@test occursin("Sounding uncertainty:   IHO S-44 Ed. 6 Order 2", output)
				errors, nnodes = depth_errors(joinpath(tmp, "flat.asc"), 100.0)
				@test length(errors) > 0.85 * nnodes
				@test errors[end] < 0.25
				for suffix in ("_num", "_sd", "_nhyp", "_strength")
					@test isfile(joinpath(tmp, "flat" * suffix * ".asc"))
				end
				nhyp = read_ascii_grid(joinpath(tmp, "flat_nhyp.asc"))
				strength = read_ascii_grid(joinpath(tmp, "flat_strength.asc"))
				depth = read_ascii_grid(joinpath(tmp, "flat.asc"))
				used = [k for (k, z) in enumerate(depth) if z < 99990.0]
				@test all(nhyp[k] >= 1.0 for k in used)
				@test all(0.0 <= strength[k] <= 5.0 for k in used)
			end
		end
	end

	@testset "CubeRejectsSpikes" begin
		if !isfile(SYNTHETIC)
			@test_skip "mbcube_synthetic not built"
		else
			mktempdir() do tmp
				run_cube(tmp, "spikes", 100, 0, "cube", ["-F9", "-E2/2/meters"])
				run_cube(tmp, "spikes", 100, 0, "median", ["-F2", "-E2/2/meters"])
				cube_errors, _ = depth_errors(joinpath(tmp, "cube.asc"), 100.0)
				median_errors, _ = depth_errors(joinpath(tmp, "median.asc"), 100.0)
				# 99th percentile of the node errors
				@test p99(cube_errors) < 0.3
				@test p99(median_errors) > 1.0
			end
		end
	end

	@testset "CubePredictedRuleNeedsPrediction" begin
		output, code = mbrun(vcat([CMD, "-F9", "--cube-extractor=predicted"], BOUNDS); stderr_to_stdout = true)
		@test code != 0
		@test occursin("needs a predicted surface", output)
	end

	@testset "CubeMedianPrediction" begin
		if !isfile(SYNTHETIC)
			@test_skip "mbcube_synthetic not built"
		else
			mktempdir() do tmp
				# sparse data (a few soundings per 2 m node): the median is taken on a grid four times
				# coarser, so single spikes do not become predictions
				output = run_cube(tmp, "spikes", 100, 0, "pred",
				                  ["-F9", "-E2/2/meters", "--cube-prediction=median/4", "--cube-extractor=predicted"])
				@test occursin("Predicted surface:      median of the soundings nearest to each node", output)
				@test occursin("4 times the node spacing", output)
				@test occursin("data points used for the median predicted surface", output)
				@test occursin("Predicted surface set at", output)
				errors, _ = depth_errors(joinpath(tmp, "pred.asc"), 100.0)
				@test p99(errors) < 0.3
			end
		end
	end

	@testset "CubeGridTitle" begin
		gmt = Sys.which("gmt")
		if !isfile(SYNTHETIC)
			@test_skip "mbcube_synthetic not built"
		elseif gmt === nothing
			@test_skip "gmt not found"
		else
			mktempdir() do tmp
				run_cube(tmp, "flat", 100, 0, "title", ["-F9", "-E4/4/meters", "-G3", "--cube-iho-edition=5",
				                                        "--cube-order=1a"])
				info = mbcheck([gmt, "grdinfo", joinpath(tmp, "title.grd")])
				@test occursin("Bathymetry Grid (CUBE, IHO S-44 Ed. 5 Order 1a)", info)
			end
		end
	end

	@testset "CubeDepthBelowSonar" begin
		if !isfile(SYNTHETIC)
			@test_skip "mbcube_synthetic not built"
		else
			mktempdir() do tmp
				# AUV 50 m above a 3000 m seafloor with spikes: uncertainties scaled by the depth below
				# the sonar reject the spikes, uncertainties scaled by the full water depth let them bias
				# the surface
				options = ["-F9", "-E4/4/meters", "--cube-order=special"]
				run_cube(tmp, "spikes", 3000, 2950, "sonar", options)
				run_cube(tmp, "spikes", 3000, 2950, "surface", vcat(options, ["--cube-depth-reference=surface"]))
				sonar_errors, _ = depth_errors(joinpath(tmp, "sonar.asc"), 3000.0)
				surface_errors, _ = depth_errors(joinpath(tmp, "surface.asc"), 3000.0)
				@test sonar_errors[end] < 0.3
				@test surface_errors[end] > 1.0
			end
		end
	end

	@testset "CubePredictionGridSign" begin
		if !isfile(SYNTHETIC)
			@test_skip "mbcube_synthetic not built"
		else
			mktempdir() do tmp
				# a grid of heights (-A2) is the predicted surface of a topography grid, but not of a
				# bathymetry grid (-A1)
				swath = joinpath(tmp, "flat.mb71")
				datalist = joinpath(tmp, "flat_datalist.mb-1")
				mbcheck([SYNTHETIC, swath, "flat", "100", "0"])
				write(datalist, swath * " 71\n")
				cmd = [abspath(CMD), "-I" * datalist]
				cd(tmp) do
					mbcheck(vcat(cmd, ["-O" * joinpath(tmp, "height"), "-A2", "-F2", "-E4/4/meters"]);
					        stderr_to_stdout = true)
					cube = vcat(cmd, ["-F9", "-E2/2/meters", "-G1", "--cube-prediction=" * joinpath(tmp, "height.grd")])
					output, code = mbrun(vcat(cube, ["-A1", "-O" * joinpath(tmp, "bathymetry")]); stderr_to_stdout = true)
					@test code == 12	# MB_ERROR_BAD_PARAMETER
					@test occursin("with -A1 its values must be depths (positive down)", output)
					output, code = mbrun(vcat(cube, ["-A2", "-O" * joinpath(tmp, "topography")]); stderr_to_stdout = true)
					@test code == 0
					@test occursin("Predicted surface set at", output)
				end
			end
		end
	end
end
