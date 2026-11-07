# See README file for copying and redistribution conditions.

# Tests for mbsvpextract command line app.

include(joinpath(@__DIR__, "..", "mbtest.jl"))

const CMD = abspath(mbprog("svptools", "mbsvpextract"))
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


const REQUIRED = ["--woa-version=23", "-Pdecav", "-R01"]

const SITES = """## Site File Version 2.00
## Number of sites: 3
 -11.5000000,  37.0000000, -3612.394, 4, 1,Site 0
 -11:15.0,     36:30.0,        0.000, 4, 1,Site, 1
 -10.2500000,  38.2500000,     0.000, 4, 1,Site 0
"""

const ROUTE = """## Route File Version 2.00
##   WAYPOINT_NONE         0  Defines topography between waypoints
> ## STARTROUTE 4,0,1,1,Route:1
-12.5,36.5,-3000.0,1,90.0,0.0,0.0,0.1 ## WAYPOINT
-12.0,36.5,-3000.0,0,90.0,0.0,0.0,0.1
-11.5,36.5,0.0,3,90.0,0.0,0.0,0.1 ## WAYPOINT STARTLINE
-10.5,37.5,0.0,4,90.0,0.0,0.0,0.1 ## WAYPOINT ENDLINE
> ## ENDROUTE
"""

# A scratch directory with the sites and route files; f(dir, output, sites, route)
function with_dir(f)
	mktempdir(; prefix = "mbsvpextract_test") do dir
		f(dir, joinpath(dir, "out"), write_file(dir, "sites.ste", SITES), write_file(dir, "route.rte", ROUTE))
	end
end

function write_file(dir, name, text)
	path = joinpath(dir, name)
	write(path, text)
	return path
end

function run_tool(output, args...; code = 0, required = REQUIRED, woa = WOA)
	out, status = mbrun(vcat([CMD, "-S", woa, "-O", output], required, collect(args)); stderr_to_stdout = true)
	@test status == code
	status == code || println(out)
	return out
end

# The newest mbsvpextract-*.json summary of `output`
function summary(output)
	names = [joinpath(output, n) for n in readdir(output) if startswith(n, "mbsvpextract-") && endswith(n, ".json")]
	return parse_json(read(names[argmax(mtime.(names))], String))
end

# The lines of an output file, as Python's splitlines()
read_lines(output, name) = split(chomp(unixlines(read(joinpath(output, name), String))), '\n')

# The JSON of a "# MBSVPEXTRACT_LOCATION {...}" line
location(line) = parse_json(split(line, ' '; limit = 3)[3])

svp_files(output) = sort(filter(n -> endswith(n, ".svp"), readdir(output)))

@testset "mbsvpextract" begin
	@testset "Usage" begin
		function usage_error(args, expected)
			output, code = mbrun(vcat([CMD], args); stderr_to_stdout = true)
			@test code == 9
			@test occursin(expected, output)
		end

		@testset "NoArgs" begin
			output, code = mbrun([CMD]; stderr_to_stdout = true)
			@test code == 9
			for text in ("--input {-I}, --sites or --route is required", "--svpfolder {-S} is required",
			             "--outputfolder {-O} is required", "--woa-version (18 or 23) is required",
			             "--period {-P} (such as decav, A5B7 or B5C2) is required", "--resolution {-R} (01 or 04) is required")
				@test occursin(text, output)
			end
		end

		@testset "Help" begin
			output = mbcheck([CMD, "-H"]; stderr_to_stdout = true)
			@test occursin("Version", output)
			@test occursin("waypoints of a route file", output)
			@test occursin("https://zenodo.org/communities/mb-system_woa_svp_datasets/", output)
			@test occursin("usage:", output)
			for option in ("--input", "--sites", "--route", "--svpfolder", "--outputfolder", "--woa-version", "--period",
			               "--resolution", "--formula", "--strict-nearest", "--overwrite", "--verbose")
				@test occursin(option, output)
			end
		end

		@testset "BadOptions" begin
			base = ["-Ia", "-Sb", "-Oc"]
			usage_error(vcat(base, ["--woa-version=19", "-Pdecav", "-R01"]), "--woa-version must be 18 or 23")
			usage_error(vcat(base, ["--woa-version=23", "-Pdecav", "-R02"]), "--resolution must be 01 or 04")
			usage_error(vcat(base, ["--woa-version=23", "-P../x", "-R01"]), "--period holds letters and digits only")
			usage_error(vcat(base, REQUIRED, ["--formula=x"]), "--formula must be")
			usage_error(vcat(base, REQUIRED, ["stray"]), "Unexpected argument")
		end
	end

	@testset "Sites" begin
		with_dir() do dir, output, sites, route
			run_tool(output, "-I", sites)
			@test svp_files(output) == ["Site_0.svp", "Site_0_2.svp", "Site_1.svp"]
			lines = read_lines(output, "Site_0.svp")
			@test lines[1] == "# mbsvpextract; formula=chen; period=decav; annual average; endpoint=11000m; site=Site 0"
			@test startswith(lines[2], "# MBSVPEXTRACT_LOCATION {")
			meta = location(lines[2])
			@test meta == Dict{String,Any}("version" => 1, "Longitude_site" => -11.5, "Latitude_site" => 37.0,
			                               "WOAdataset" => "WOA23", "WOA_resolution" => "01", "WOA_period" => "decav",
			                               "Longitude_WOA" => -11.5, "Latitude_WOA" => 36.5,
			                               "Deeper_neighbour" => false, "Physics_from_depth" => 5100.0)
			@test startswith(lines[3], "0.000000 ")
			@test startswith(lines[end], "11000.000000 ")
			@test all(1200 < parse(Float64, split(line)[2]) < 2000 for line in lines[3:end])
			# a usual file mode, not owner only
			@test filemode(joinpath(output, "Site_0.svp")) & 0o044 != 0
			report = summary(output)
			@test report["input_type"] == "sites"
			@test report["input_file"] == realpath(sites)
			@test report["edition"] == "WOA23"
			@test report["period"] == "decav"
			@test report["interrupted"] == false
			@test [row["status"] for row in report["sites"]] == fill("success", 3)
			@test report["sites"][2]["name"] == "Site, 1"
			@test report["sites"][2]["longitude"] == -11.25
			@test report["sites"][2]["latitude"] == 36.5
			@test report["sites"][1]["line"] == 3
			@test report["sites"][1]["candidate_rank"] == 1
		end
	end

	@testset "Route" begin
		with_dir() do dir, output, sites, route
			run_tool(output, "-I", route)
			@test svp_files(output) == ["Route_1_WP000.svp", "Route_1_WP001.svp", "Route_1_WP002.svp"]
			meta = location(read_lines(output, "Route_1_WP002.svp")[2])
			@test meta["Longitude_site"] == -10.5
			@test meta["Latitude_site"] == 37.5
			@test meta["Route"] == "Route:1"
			@test meta["Waypoint"] == 2
			report = summary(output)
			@test report["input_type"] == "route"
			@test [row["name"] for row in report["sites"]] == ["Route:1 WP000", "Route:1 WP001", "Route:1 WP002"]
			@test [row["line"] for row in report["sites"]] == [4, 6, 7]
			@test [row["waypoint_type"] for row in report["sites"]] == [1, 3, 4]
		end
	end

	@testset "RouteAndSitesGiveTheSameProfile" begin
		with_dir() do dir, output, sites, route
			run_tool(output, "--route", route)
			route_lines = read_lines(output, "Route_1_WP001.svp")
			site = write_file(dir, "one.ste", "## Site File Version 2.00\n-11.5,36.5,0,0,0,One\n")
			run_tool(output, "--sites", site)
			@test read_lines(output, "One.svp")[3:end] == route_lines[3:end]
		end
	end

	@testset "ForcedTypeWins" begin
		with_dir() do dir, output, sites, route
			path = write_file(dir, "plain.txt", replace(ROUTE, "## Route File Version 2.00\n" => ""))
			run_tool(output, "-I", path; code = 1)
			run_tool(output, "--route", path)
			@test isfile(joinpath(output, "Route_1_WP000.svp"))
		end
	end

	@testset "FormulaBoth" begin
		with_dir() do dir, output, sites, route
			run_tool(output, "-I", route, "--formula=both")
			chen = read_lines(output, "Route_1_WP000_chen.svp")
			leroy = read_lines(output, "Route_1_WP000_leroy.svp")
			@test occursin("formula=chen", chen[1])
			@test occursin("formula=leroy", leroy[1])
			@test chen[3] != leroy[3]
			@test length(summary(output)["sites"][1]["outputs"]) == 2
		end
	end

	@testset "FormulaLeroy" begin
		with_dir() do dir, output, sites, route
			run_tool(output, "-I", route, "--formula=leroy")
			@test occursin("formula=leroy", read_lines(output, "Route_1_WP000.svp")[1])
		end
	end

	@testset "UserChoosesTheGrid" begin
		with_dir() do dir, output, sites, route
			# the same bytes under another period name: the requested name is what is read and recorded
			grids = joinpath(dir, "grids")
			mkdir(grids)
			cp(joinpath(WOA, "woa18_decav_svp00_01.nc"), joinpath(grids, "woa18_A5B7_svp00_01.nc"))
			run_tool(output, "-I", route; required = ["--woa-version=18", "-P", "A5B7", "-R", "01"], woa = grids)
			lines = read_lines(output, "Route_1_WP000.svp")
			@test occursin("period=A5B7", lines[1])
			meta = location(lines[2])
			@test meta["WOAdataset"] == "WOA18"
			@test meta["WOA_period"] == "A5B7"
			@test endswith(summary(output)["dataset"], "woa18_A5B7_svp00_01.nc")
		end
	end

	@testset "MissingDatasetIsNotReplaced" begin
		with_dir() do dir, output, sites, route
			for required in (["--woa-version=18", "-PB5C2", "-R01"], ["--woa-version=23", "-PA5B7", "-R01"],
			                 ["--woa-version=23", "-Pdecav", "-R04"])
				out = run_tool(output, "-I", route; required, code = 1)
				@test occursin("Required dataset not found", out)
				@test occursin("https://zenodo.org/communities/mb-system_woa_svp_datasets/", out)
			end
			@test !ispath(output)
		end
	end

	@testset "Overwrite" begin
		with_dir() do dir, output, sites, route
			run_tool(output, "-I", route)
			path = joinpath(output, "Route_1_WP000.svp")
			write(path, "kept")
			out = run_tool(output, "-I", route; code = 1)
			@test occursin("Output exists; use --overwrite", out)
			@test read(path, String) == "kept"
			@test [row["status"] for row in summary(output)["sites"]] == fill("failed", 3)
			run_tool(output, "-I", route, "--overwrite")
			@test read(path, String) != "kept"
		end
	end

	@testset "PositionOutsideGridFailsAndNextGoesOn" begin
		with_dir() do dir, output, sites, route
			path = write_file(dir, "two.ste", "## Site File Version 2.00\n50.0,37.0,0,0,0,Far\n-11.5,37.0,0,0,0,Near\n")
			out = run_tool(output, "-I", path; code = 1)
			@test occursin("Site outside cropped longitude coverage", out)
			report = summary(output)
			@test [row["status"] for row in report["sites"]] == ["failed", "success"]
			@test occursin("cropped", report["sites"][1]["error"])
			@test !ispath(joinpath(output, "Far.svp"))
		end
	end

	@testset "InputErrors" begin
		with_dir() do dir, output, sites, route
			@test occursin("Input does not exist", run_tool(output, "-I", joinpath(dir, "none.ste"); code = 1))
			@test occursin("--sites or --route", run_tool(output, "-I", write_file(dir, "x.txt", "1 2\n"); code = 1))
			@test occursin(":2: Latitude outside",
			               run_tool(output, "-I", write_file(dir, "y.ste", "## Site File Version 2.00\n1,95,0,0,0,a\n"); code = 1))
			@test occursin("No lines marked ## WAYPOINT",
			               run_tool(output, "-I", write_file(dir, "z.rte", "## Route File Version 2.00\n1,2,3,0\n"); code = 1))
		end
	end
end
