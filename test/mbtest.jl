# Shared helper of the MB-System tests (Julia; replaces the Python unittest scripts).
#
# A test file is run with its own directory as the working directory (testdata/ lives there):
#     julia test/utilities/mbtime_test.jl
# and finds the programs it tests in the build tree named by MB_BUILD_DIR (CMake passes
# CMAKE_BINARY_DIR); without it, in the in-source layout the Python tests assumed
# ("../../src/<dir>/<program>").
#
# Only the Test standard library is used.

using Test

const MB_BUILD_DIR = get(ENV, "MB_BUILD_DIR", "")
const MB_EXE = Sys.iswindows() ? ".exe" : ""

# The program `name` built in src/<subdir> (e.g. mbprog("utilities", "mbtime"))
function mbprog(subdir::String, name::String)::String
	isempty(MB_BUILD_DIR) && return joinpath("..", "..", "src", subdir, name * MB_EXE)
	return joinpath(MB_BUILD_DIR, "src", subdir, name * MB_EXE)
end

# The environment the programs run in: on Windows the build's own DLL folders go first on PATH,
# since nothing installs them; elsewhere the build's RPATH already finds them.
function mbenv()::Dict{String,String}
	env = Dict{String,String}(ENV)
	if Sys.iswindows() && !isempty(MB_BUILD_DIR)
		dirs = [joinpath(MB_BUILD_DIR, "src", "mbio"), joinpath(MB_BUILD_DIR, "src", "mbaux"),
		        joinpath(MB_BUILD_DIR, "bin")]
		env["PATH"] = join(vcat(dirs, get(env, "PATH", "")), ';')
	end
	return env
end

# Run `cmd`; return (output, exit code). `stderr_to_stdout` merges stderr into the output, as
# Python's check_output(..., stderr=STDOUT) did; otherwise stderr is discarded.
function mbrun(cmd::Vector{String}; stderr_to_stdout::Bool = false, env = mbenv())::Tuple{String,Int}
	# An explicit pipe read to its end: with an IOBuffer as the target the copy of a short-lived
	# program's output could still be in flight when run() returned (seen: mbtime's one line lost).
	pipe = Pipe()
	proc = run(pipeline(ignorestatus(setenv(Cmd(cmd), env)); stdin = devnull, stdout = pipe,
	                    stderr = stderr_to_stdout ? pipe : devnull); wait = false)
	close(pipe.in)
	output = read(pipe, String)
	wait(proc)
	# A program killed by a signal (an abort, a crash) reports minus the signal, as Python's
	# returncode did, so "it crashed" stays distinguishable from "it failed".
	return output, proc.termsignal != 0 ? -Int(proc.termsignal) : Int(proc.exitcode)
end

# Python's subprocess.check_output: the output, and a test ERROR when the program fails.
function mbcheck(cmd::Vector{String}; stderr_to_stdout::Bool = false, env = mbenv())::String
	output, code = mbrun(cmd; stderr_to_stdout, env)
	code == 0 || error("$(join(cmd, ' ')) exited with $code:\n$output")
	return output
end

# Text with Windows line ends made Unix ones: a program's text output and a checked-out reference
# file then compare equal whatever the platform and git's line-end conversion did.
unixlines(s::AbstractString)::String = replace(String(s), "\r\n" => "\n")

# ---- GUI programs (Motif): they open the X display before parsing arguments -----------------
# with_xvfb(f) runs f(env) with DISPLAY on a private Xvfb server, and returns `nothing` (the
# caller skips) when there is no Xvfb, as xvfb_helper.py did.
function find_xvfb()
	p = Sys.which("Xvfb")
	p !== nothing && return p
	isfile("/opt/X11/bin/Xvfb") && return "/opt/X11/bin/Xvfb"
	return nothing
end

function with_xvfb(f::Function)
	xvfb = find_xvfb()
	xvfb === nothing && return nothing
	num = rand(100:999)
	for _ in 1:20
		isfile("/tmp/.X$(num)-lock") || break
		num = rand(100:999)
	end
	proc = run(pipeline(`$xvfb :$num -screen 0 1024x768x24`; stdout = devnull, stderr = devnull); wait = false)
	try
		deadline = time() + 15
		while !isfile("/tmp/.X$(num)-lock")
			process_exited(proc) && error("Xvfb exited early (code $(proc.exitcode)) before binding :$num")
			time() > deadline && error("Xvfb did not bind :$num within 15 s")
			sleep(0.05)
		end
		sleep(0.2)	# the lock file can appear an instant before the server accepts clients
		env = mbenv()
		env["DISPLAY"] = ":$num"
		return f(env)
	finally
		kill(proc)
	end
end
