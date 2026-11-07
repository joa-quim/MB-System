# See README file for copying and redistribution conditions.

# Tests for mbsvpbest command line app.

include(joinpath(@__DIR__, "..", "mbtest.jl"))

const CMD = abspath(mbprog("svptools", "mbsvpbest"))
const SWATH = joinpath(@__DIR__, "testdata", "mb58", "0008_20111219_210356_METEOR_EM122.snipped.all")
const WOA = joinpath(@__DIR__, "testdata", "woa")

# ---- a minimal JSON reader (only the Test standard library is used): objects become
# Dict{String,Any}, arrays Vector{Any}, numbers Int or Float64, null nothing
function parse_json(text::AbstractString)
	s = String(text)
	value, i = json_value(s, json_skip(s, 1))
	json_skip(s, i) > ncodeunits(s) || error("JSON: trailing text at $i")
	return value
end
function json_skip(s, i)
	while i <= ncodeunits(s) && s[i] in (' ', '\t', '\n', '\r')
		i += 1
	end
	return i
end
function json_value(s, i)
	c = s[i]
	if c == '{'
		obj = Dict{String,Any}()
		i = json_skip(s, i + 1)
		s[i] == '}' && return obj, i + 1
		while true
			key, i = json_string(s, json_skip(s, i))
			i = json_skip(s, i)
			s[i] == ':' || error("JSON: ':' expected at $i")
			obj[key], i = json_value(s, json_skip(s, i + 1))
			i = json_skip(s, i)
			s[i] == '}' && return obj, i + 1
			s[i] == ',' || error("JSON: ',' expected at $i")
			i += 1
		end
	elseif c == '['
		arr = Any[]
		i = json_skip(s, i + 1)
		s[i] == ']' && return arr, i + 1
		while true
			v, i = json_value(s, json_skip(s, i))
			push!(arr, v)
			i = json_skip(s, i)
			s[i] == ']' && return arr, i + 1
			s[i] == ',' || error("JSON: ',' expected at $i")
			i += 1
		end
	elseif c == '"'
		return json_string(s, i)
	elseif startswith(SubString(s, i), "true")
		return true, i + 4
	elseif startswith(SubString(s, i), "false")
		return false, i + 5
	elseif startswith(SubString(s, i), "null")
		return nothing, i + 4
	else
		m = match(r"-?\d+(\.\d+)?([eE][-+]?\d+)?", s, i)
		(m === nothing || m.offset != i) && error("JSON: value expected at $i")
		v = m.match
		return (m.captures[1] === nothing && m.captures[2] === nothing ? parse(Int, v) : parse(Float64, v)), i + ncodeunits(v)
	end
end
function json_string(s, i)
	s[i] == '"' || error("JSON: string expected at $i")
	io = IOBuffer()
	i += 1
	while s[i] != '"'
		if s[i] == '\\'
			e = s[i + 1]
			if e == 'u'
				print(io, Char(parse(UInt16, s[i + 2:i + 5]; base = 16)))
				i += 6
				continue
			end
			print(io, get(Dict('n' => '\n', 't' => '\t', 'r' => '\r', 'b' => '\b', 'f' => '\f'), e, e))
			i += 2
		else
			print(io, s[i])
			i = nextind(s, i)
		end
	end
	return String(take!(io)), i + 1
end

# The run-*/ranking.json files under `output`
ranking_files(output) = isdir(output) ?
	[joinpath(output, d, "ranking.json") for d in readdir(output) if startswith(d, "run-") && isfile(joinpath(output, d, "ranking.json"))] : String[]

function ranking(output)
	names = ranking_files(output)
	@test length(names) == 1
	return parse_json(read(names[1], String))
end

profile_names(report) = [basename(r["profile"]) for r in report["ranking"]]

# A scratch directory with a copy of the swath file; f(dir, swath, output, env)
function with_swath(f)
	mktempdir(; prefix = "mbsvpbest_test") do dir
		swath = joinpath(dir, basename(SWATH))
		cp(SWATH, swath)
		env = mbenv()
		env["PATH"] = join([abspath(dirname(mbprog("utilities", "mbprocess"))), get(env, "PATH", "")],
		                   Sys.iswindows() ? ';' : ':')
		f(dir, swath, joinpath(dir, "out"), env)
	end
end

function run_tool(output, env, args...; code = 0)
	out, status = mbrun(vcat([CMD, "-S", WOA, "-O", output], collect(args)); stderr_to_stdout = true, env)
	@test status == code
	status == code || println(out)
	return out
end

function write_list(dir, lines...)
	path = joinpath(dir, "list.mb-1")
	write(path, "# comment\n" * join(line * "\n" for line in lines))
	return path
end

@testset "mbsvpbest" begin
	@testset "Usage" begin
		function usage_error(args, expected)
			output, code = mbrun(vcat([CMD], args); stderr_to_stdout = true)
			@test code == 9
			@test occursin(expected, output)
		end

		@testset "NoArgs" begin
			usage_error(String[], "--svpfolder {-S} is required")
		end

		@testset "Help" begin
			output = mbcheck([CMD, "-H"]; stderr_to_stdout = true)
			@test occursin("Version", output)
			@test occursin("not the true water column", output)
			@test occursin("usage:", output)
			for option in ("--svpfolder", "--apply-best", "--enddate", "--input", "--minimum-beams", "--nearest",
			               "--outputfolder", "--period", "--plots", "--resolution", "--selection", "--verbose")
				@test occursin(option, output)
			end
		end

		@testset "BadOptions" begin
			usage_error(["-Sx", "--minimum-beams=2"], "--minimum-beams must be >=3")
			usage_error(["-Sx", "-R02"], "--resolution must be 01 or 04")
			usage_error(["-Sx", "--selection=other"], "--selection must be")
			usage_error(["-Sx", "stray"], "Unexpected argument")
		end
	end

	@testset "CombinedRestoresFiles" begin
		with_swath() do dir, swath, output, env
			before = sort(readdir(dir))
			out = run_tool(output, env, "-I", swath, "-P", "decav", "-R", "01")
			@test occursin("Original parameter files restored.", out)
			report = ranking(output)
			@test report["applied"] == false
			@test report["inputs"] == [realpath(swath)]
			@test sort(profile_names(report)) == sort(vcat(["internal_0000_0000.svp"],
				["woa18_decav_svp$(t)_01_$(f).svp" for t in ("00", "12", "16") for f in ("chen", "leroy")],
				["woa23_decav_svp00_01_$(f).svp" for f in ("chen", "leroy")]))
			rmse = [r["rmse"] for r in report["ranking"]]
			@test rmse == sort(rmse)
			for record in report["ranking"]
				@test record["soundings"] == 789
				@test record["pings"] == 2
				@test record["rejected_pings"] == 0
			end
			# season and month of WOA23 are missing from the test folder
			@test length(report["profile_notes"]) == 2
			# nothing is left next to the data, and no trial output in the run directory
			@test sort(readdir(dir)) == sort(vcat(before, ["out"]))
			run = dirname(ranking_files(output)[1])
			@test isempty(filter(n -> startswith(n, "processed_"), readdir(run)))
			@test !ispath(joinpath(run, "backup"))
			# text file, CRLF on Windows (Python's text-mode readline in the upstream test reads it as \n)
			@test unixlines(readline(joinpath(run, "ranking.csv"); keep = true)) == "profile,rmse,mae,mse,me,soundings,pings,rejected_pings\n"
		end
	end

	@testset "ExistingSidecarsAreRestored" begin
		with_swath() do dir, swath, output, env
			sidecars = ((swath * ".par", "## not much\nSVPMODE 0\n"), (swath * ".resf", "old"))
			for (name, text) in sidecars
				write(name, text)
				f = Base.Filesystem.open(name, Base.Filesystem.JL_O_RDWR)
				Base.Filesystem.futime(f, 1.0e9, 1.0e9)
				close(f)
			end
			run_tool(output, env, "-I", swath, "-P", "decav", "-R", "01")
			for (name, text) in sidecars
				@test read(name, String) == text
				@test floor(Int, mtime(name)) == 1000000000
			end
		end
	end

	@testset "ValuesBySpacesAndCommas" begin
		with_swath() do dir, swath, output, env
			run_tool(output, env, "-I", swath, "-P", "decav", "none", "-R", "01,01")
			@test length(ranking(output)["ranking"]) == 9
		end
	end

	@testset "Nearest" begin
		with_swath() do dir, swath, output, env
			run_tool(output, env, "-I", swath, "-P", "decav", "-R", "01", "-N")
			run = dirname(ranking_files(output)[1])
			text = read(joinpath(run, "woa18_decav_svp00_01_chen.svp"), String)
			@test occursin("\"method\":\"nearest\"", text)
			@test occursin("\"cells_used\":1,", text)
			@test occursin("physics_from_depth", text)
		end
	end

	@testset "NoCandidateOfGrids" begin
		with_swath() do dir, swath, output, env
			# the internal profile is still a candidate
			out = run_tool(output, env, "-I", swath, "-P", "none", "-R", "01")
			@test occursin("Profile note: No WOA18/WOA23", out)
			@test profile_names(ranking(output)) == ["internal_0000_0000.svp"]
		end
	end

	@testset "ApplyBest" begin
		with_swath() do dir, swath, output, env
			out = run_tool(output, env, "-I", swath, "-P", "decav", "-R", "01", "--apply-best")
			@test occursin("Winner applied to original inputs.", out)
			report = ranking(output)
			@test report["applied"] == true
			@test occursin(profile_names(report)[1], read(swath * ".par", String))
			root = swath[1:end - length(".all")]
			@test isfile(root * "p.mb58")
		end
	end

	@testset "IdenticalInternalProfileRunsOnce" begin
		with_swath() do dir, swath, output, env
			other = swath[1:end - length(".all")] * "b.all"
			cp(swath, other)
			path = write_list(dir, basename(swath) * " 58", basename(other) * " 58")
			run_tool(output, env, "-I", path, "-P", "none", "-R", "01")
			report = ranking(output)
			@test profile_names(report) == ["internal_0000_0000.svp"]
			@test report["ranking"][1]["soundings"] == 2 * 789
			@test any(occursin("same table", note) for note in report["profile_notes"])
		end
	end

	@testset "PerFile" begin
		with_swath() do dir, swath, output, env
			other = swath[1:end - length(".all")] * "b.all"
			cp(swath, other)
			bad = swath[1:end - length(".all")] * "c.all"
			write(bad, "this is not swath data\n")
			path = write_list(dir, basename(swath) * " 58", basename(bad) * " 58", basename(other) * " 58")
			run_tool(output, env, "-I", path, "-P", "decav", "-R", "01", "--selection", "per-file"; code = 1)
			names = isdir(output) ? [joinpath(output, d, "summary.json") for d in readdir(output)
			                         if startswith(d, "per-file-") && isfile(joinpath(output, d, "summary.json"))] : String[]
			@test length(names) == 1
			if length(names) == 1
				summary = parse_json(read(names[1], String))
				@test summary["selection"] == "per-file"
				@test summary["interrupted"] == false
				@test [row["status"] for row in summary["files"]] == ["success", "failed", "success"]
				@test summary["files"][2]["winner"] === nothing
				@test haskey(summary["files"][1]["winner"], "rmse")
				@test summary["files"][1]["output_directory"] != summary["files"][3]["output_directory"]
				@test ispath(summary["files"][3]["ranking_file"])
			end
		end
	end

	@testset "InputErrors" begin
		with_swath() do dir, swath, output, env
			failing(input_path, expected) = @test occursin(expected, run_tool(output, env, "-I", input_path; code = 1))
			failing(joinpath(dir, "missing.all"), "Input does not exist")
			failing(write_list(dir, "missing.all 58"), "Input does not exist")
			failing(write_list(dir, "images.mb-2 -2"), "Only ordinary/nested -1 datalists")
			failing(write_list(dir, "list.mb-1 -1"), "Recursive datalist")
			failing(write_list(dir), "Empty datalist")
			other = joinpath(dir, "unknown.qqq")
			write(other, "x")
			failing(other, "Cannot tell the swath format")
		end
	end

	@testset "BadGridFolder" begin
		with_swath() do dir, swath, output, env
			out, code = mbrun([CMD, "-S", joinpath(dir, "none"), "-I", swath, "-O", output]; stderr_to_stdout = true, env)
			@test code == 1
			@test occursin("SVP directory does not exist", out)
		end
	end
end
