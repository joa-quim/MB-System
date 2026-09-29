#--------------------------------------------------------------------
#    The MB-system:  mbjl_util.jl
#
#    Copyright (c) 2026 by
#    David W. Caress (caress@mbari.org)
#      Monterey Bay Aquarium Research Institute
#      Moss Landing, California, USA
#
#    See README.md file for copying and redistribution conditions.
#--------------------------------------------------------------------
#
# Purpose:
#   Small support layer for the Julia port of the mbpy_* macros
#   (src/macros/python). It supplies the handful of Python built-in
#   behaviors the ported modules depend on for byte-identical output,
#   none of which Julia's own Base provides in the same form:
#
#     - pystr():      Python's str()/repr() of a float ("200.0", "1e-05"),
#                     used wherever the Python modules interpolate a raw
#                     float into a GMT command or a -B tick string;
#     - pyfloat()/pyint(): Python's float()/int() of a string;
#     - pytrunc():    Python's int() of a float (truncation toward zero);
#     - shquote():    shlex.quote();
#     - pyreplace():  dataclasses.replace();
#     - ArgParser:    the subset of argparse the mbjl_* command line
#                     tools use (groups, short/long options, store_true,
#                     store_const, append, nargs="?", choices, required,
#                     typed values, --help), so the tools need no
#                     registered Julia packages at all.
#
#   Each mbjl_*.jl module includes this file (once) and then uses it
#   through `using ..MbjlUtil`, so any combination of modules can be
#   loaded into the same parent module without duplicate definitions.
#
module MbjlUtil

using Printf

export pystr, pyfloat, pyint, pytrunc, shquote, pyreplace, fmtg,
       ArgParser, ArgGroup, add_argument!, add_argument_group,
       add_mutually_exclusive_group, set_defaults!, parse_args, print_help,
       ArgParseError

# ---------------------------------------------------------------------------
# Python numeric formatting / parsing
# ---------------------------------------------------------------------------

"""
    pystr(x)

Python's `str(x)` for the values the ported modules interpolate into
generated text: floats render exactly as Python's shortest-round-trip
repr ("200.0", "0.1", "1e-05", "1.5e+16"), integers as plain digits,
`nothing` as "None", strings unchanged.
"""
pystr(x::AbstractString) = String(x)
pystr(x::Integer) = string(x)
pystr(x::Bool) = x ? "True" : "False"
pystr(::Nothing) = "None"
function pystr(x::AbstractFloat)
    x = Float64(x)
    isnan(x) && return "nan"
    isinf(x) && return x > 0 ? "inf" : "-inf"
    if x == 0.0
        return signbit(x) ? "-0.0" : "0.0"
    end
    neg = x < 0
    # Julia's own string() already yields the shortest round-trip digits
    # (the same digit string Python's repr uses); only the choice between
    # positional and scientific notation, and the exponent spelling,
    # differ. Recover (digits, exponent) and re-render Python's way.
    s = string(abs(x))
    mant, ex = if occursin('e', s)
        a, b = split(s, 'e')
        (String(a), parse(Int, b))
    else
        (s, 0)
    end
    ip, fp = occursin('.', mant) ? split(mant, '.') : (mant, "")
    digits = ip * fp
    # decimal exponent of the first significant digit
    lead = findfirst(c -> c != '0', digits)
    lead === nothing && return neg ? "-0.0" : "0.0"
    e10 = length(ip) - lead + ex
    digits = rstrip(digits[lead:end], '0')
    isempty(digits) && (digits = "0")
    out = if -4 <= e10 < 16
        if e10 >= 0
            nint = e10 + 1
            if length(digits) <= nint
                digits * "0"^(nint - length(digits)) * ".0"
            else
                digits[1:nint] * "." * digits[nint+1:end]
            end
        else
            "0." * "0"^(-e10 - 1) * digits
        end
    else
        m = length(digits) > 1 ? digits[1:1] * "." * digits[2:end] : digits
        m * "e" * (e10 < 0 ? "-" : "+") * lpad(string(abs(e10)), 2, '0')
    end
    return neg ? "-" * out : out
end

"""
    fmtg(x, prec)

C/Python `%.<prec>g` formatting (Python's `f"{x:.<prec>g}"`).
"""
fmtg(x::Real, prec::Integer) = Printf.format(Printf.Format("%.$(prec)g"), Float64(x))

"""Python's `float(s)` for a string (surrounding whitespace allowed)."""
pyfloat(s::AbstractString) = parse(Float64, strip(s))
pyfloat(x::Real) = Float64(x)

"""Python's `int(s)` for a string (integer literal only, like Python)."""
pyint(s::AbstractString) = parse(Int, strip(s))
pyint(x::Integer) = Int(x)
pyint(x::AbstractFloat) = pytrunc(x)

"""Python's `int(x)` of a float: truncation toward zero."""
pytrunc(x::Real) = trunc(Int, x)

"""Port of Python's `shlex.quote()`."""
function shquote(s::AbstractString)
    isempty(s) && return "''"
    occursin(r"[^\w@%+=:,./-]"a, s) || return String(s)
    return "'" * replace(s, "'" => "'\"'\"'") * "'"
end

"""Port of `dataclasses.replace(x, **kw)` for (mutable) structs."""
function pyreplace(x::T; kw...) where {T}
    vals = [haskey(kw, f) ? kw[f] : getfield(x, f) for f in fieldnames(T)]
    return T(vals...)
end

# ---------------------------------------------------------------------------
# Minimal argparse
# ---------------------------------------------------------------------------

struct ArgParseError <: Exception
    msg::String
end
Base.showerror(io::IO, e::ArgParseError) = print(io, e.msg)

mutable struct ArgSpec
    flags::Vector{String}
    dest::Symbol
    action::Symbol                 # :store, :store_true, :store_const, :append
    nargs::Union{Nothing,String}   # nothing (exactly one) or "?"
    const_::Any
    default::Any
    type::Any                      # String, Int, Float64
    choices::Any                   # nothing or a collection
    required::Bool
    metavar::Union{Nothing,String}
    help::Union{Nothing,String}
    exclusive::Int                 # 0 = none, else mutually-exclusive group id
end

mutable struct ArgGroup
    title::Union{Nothing,String}
    specs::Vector{ArgSpec}
    parser::Any
    exclusive::Int
end

mutable struct ArgParser
    prog::String
    description::String
    groups::Vector{ArgGroup}
    specs::Vector{ArgSpec}
    defaults::Dict{Symbol,Any}
    nexclusive::Int
end

ArgParser(; prog::AbstractString = "", description::AbstractString = "") =
    (p = ArgParser(String(prog), String(description), ArgGroup[], ArgSpec[], Dict{Symbol,Any}(), 0);
     push!(p.groups, ArgGroup("options", ArgSpec[], p, 0));
     p)

"""argparse's `add_argument_group(title)`."""
function add_argument_group(p::ArgParser, title::AbstractString)
    g = ArgGroup(String(title), ArgSpec[], p, 0)
    push!(p.groups, g)
    return g
end

"""argparse's `add_mutually_exclusive_group()` (its options list under the
parser's own default group, exactly as argparse prints them)."""
function add_mutually_exclusive_group(p::ArgParser)
    p.nexclusive += 1
    return ArgGroup(nothing, p.groups[1].specs, p, p.nexclusive)
end

"""argparse's `set_defaults(**kw)`."""
set_defaults!(p::ArgParser; kw...) = (for (k, v) in kw; p.defaults[k] = v; end; p)

function _dest_from_flags(flags)
    for f in flags
        startswith(f, "--") && return Symbol(replace(f[3:end], "-" => "_"))
    end
    return Symbol(replace(lstrip(flags[1], '-'), "-" => "_"))
end

"""argparse's `add_argument(*flags, **kw)` (option arguments only)."""
function add_argument!(g::ArgGroup, flags::AbstractString...;
                       action::Symbol = :store, nargs = nothing, const_ = nothing,
                       default = nothing, type = String, choices = nothing,
                       required::Bool = false, metavar = nothing, help = nothing,
                       dest = nothing)
    fl = String[f for f in flags]
    d = dest === nothing ? _dest_from_flags(fl) : Symbol(dest)
    if action == :store_true && default === nothing
        default = false
    elseif action == :append && default === nothing
        default = String[]
    end
    spec = ArgSpec(fl, d, action, nargs === nothing ? nothing : String(nargs), const_,
                   default, type, choices, required,
                   metavar === nothing ? nothing : String(metavar),
                   help === nothing ? nothing : String(help), g.exclusive)
    push!(g.specs, spec)
    push!(g.parser.specs, spec)
    return spec
end
add_argument!(p::ArgParser, flags::AbstractString...; kw...) = add_argument!(p.groups[1], flags...; kw...)

_metavar(s::ArgSpec) = s.metavar !== nothing ? s.metavar :
    s.choices !== nothing ? "{" * join(string.(collect(s.choices)), ",") * "}" :
    uppercase(string(s.dest))

function _usage_piece(s::ArgSpec)
    f = s.flags[1]
    body = if s.action in (:store_true, :store_const)
        f
    elseif s.nargs == "?"
        "$f [$(_metavar(s))]"
    else
        "$f $(_metavar(s))"
    end
    return s.required ? body : "[" * body * "]"
end

function print_help(io::IO, p::ArgParser)
    println(io, "usage: ", p.prog, " [-h] ", join(_usage_piece.(p.specs), " "))
    println(io)
    isempty(p.description) || (println(io, p.description); println(io))
    for (i, g) in enumerate(p.groups)
        specs = i == 1 ? g.specs : g.specs
        (i == 1 || !isempty(specs)) || continue
        println(io, g.title, ":")
        i == 1 && println(io, "  -h, --help            show this help message and exit")
        for s in specs
            names = if s.action in (:store_true, :store_const)
                join(s.flags, ", ")
            elseif s.nargs == "?"
                join(["$f [$(_metavar(s))]" for f in s.flags], ", ")
            else
                join(["$f $(_metavar(s))" for f in s.flags], ", ")
            end
            if s.help === nothing
                println(io, "  ", names)
            elseif length(names) <= 20
                println(io, "  ", rpad(names, 22), replace(s.help, "%%" => "%"))
            else
                println(io, "  ", names)
                println(io, " "^24, replace(s.help, "%%" => "%"))
            end
        end
        println(io)
    end
end
print_help(p::ArgParser) = print_help(stdout, p)

function _convert(s::ArgSpec, v::AbstractString)
    val = try
        s.type === Int ? parse(Int, strip(v)) :
        s.type === Float64 ? parse(Float64, strip(v)) : String(v)
    catch
        tname = s.type === Int ? "int" : "float"
        throw(ArgParseError("argument $(join(s.flags, "/")): invalid $tname value: '$v'"))
    end
    if s.choices !== nothing && !(val in s.choices)
        throw(ArgParseError("argument $(join(s.flags, "/")): invalid choice: '$v' (choose from " *
                            join(["'$c'" for c in collect(s.choices)], ", ") * ")"))
    end
    return val
end

function _lookup(p::ArgParser, flag::AbstractString)
    for s in p.specs
        flag in s.flags && return s
    end
    # argparse accepts any unambiguous prefix of a long option
    if startswith(flag, "--")
        hits = [s for s in p.specs if any(f -> startswith(f, "--") && startswith(f, flag), s.flags)]
        length(hits) == 1 && return hits[1]
        if length(hits) > 1
            throw(ArgParseError("ambiguous option: $flag could match " *
                                join([f for s in hits for f in s.flags if startswith(f, flag)], ", ")))
        end
    end
    return nothing
end

_is_option(p::ArgParser, a::AbstractString) =
    startswith(a, "-") && length(a) > 1 && !occursin(r"^-\d+$|^-\d*\.\d+$", a) &&
    (_lookup(p, first(split(a, "="; limit = 2))) !== nothing ||
     (!startswith(a, "--") && _lookup(p, a[1:2]) !== nothing))

"""
    parse_args(p, argv) -> Dict{Symbol,Any}

argparse's `parse_args()`. Prints usage and exits (status 0 for --help,
status 2 for a usage error) exactly as argparse does; with
`exit_on_error=false` a usage error throws ArgParseError instead.

Unlike argparse, a value that merely *starts* with "-" (e.g.
`-R -122/-121/36/37`) is accepted as an option's argument as long as it
is not itself a recognized option -- argparse rejects that form and
forces `-R=-122/...`.
"""
function parse_args(p::ArgParser, argv::AbstractVector{<:AbstractString}; exit_on_error::Bool = true)
    try
        return _parse_args(p, argv)
    catch e
        (e isa ArgParseError && exit_on_error) || rethrow()
        println(stderr, "usage: ", p.prog, " [-h] ", join(_usage_piece.(p.specs), " "))
        println(stderr, p.prog, ": error: ", e.msg)
        exit(2)
    end
end

function _parse_args(p::ArgParser, argv)
    res = Dict{Symbol,Any}()
    for s in p.specs
        haskey(res, s.dest) && continue
        res[s.dest] = s.default isa AbstractVector ? copy(s.default) : s.default
    end
    for (k, v) in p.defaults
        res[k] = v
    end
    seen = Set{Symbol}()
    exgroup = Dict{Int,ArgSpec}()
    args = collect(String, argv)
    i = 1
    while i <= length(args)
        a = args[i]
        if a == "-h" || a == "--help"
            print_help(stdout, p)
            exit(0)
        end
        startswith(a, "-") && length(a) > 1 ||
            throw(ArgParseError("unrecognized arguments: $a"))
        inline = nothing
        spec = nothing
        if startswith(a, "--")
            name, rest = occursin("=", a) ? split(a, "="; limit = 2) : (a, nothing)
            spec = _lookup(p, name)
            inline = rest === nothing ? nothing : String(rest)
        else
            spec = _lookup(p, a)
            if spec === nothing && length(a) > 2
                spec = _lookup(p, a[1:2])
                if spec !== nothing
                    inline = a[3] == '=' ? a[4:end] : a[3:end]
                end
            end
        end
        spec === nothing && throw(ArgParseError("unrecognized arguments: $a"))
        if spec.exclusive != 0
            other = get(exgroup, spec.exclusive, nothing)
            if other !== nothing && other !== spec
                throw(ArgParseError("argument $(join(spec.flags, "/")): not allowed with argument $(join(other.flags, "/"))"))
            end
            exgroup[spec.exclusive] = spec
        end
        push!(seen, spec.dest)
        if spec.action == :store_true
            inline === nothing || throw(ArgParseError("argument $(join(spec.flags, "/")): ignored explicit argument '$inline'"))
            res[spec.dest] = true
        elseif spec.action == :store_const
            inline === nothing || throw(ArgParseError("argument $(join(spec.flags, "/")): ignored explicit argument '$inline'"))
            res[spec.dest] = spec.const_
        else
            val = nothing
            if inline !== nothing
                val = _convert(spec, inline)
            elseif i < length(args) && !_is_option(p, args[i+1])
                i += 1
                val = _convert(spec, args[i])
            elseif spec.nargs == "?"
                val = spec.const_
            else
                throw(ArgParseError("argument $(join(spec.flags, "/")): expected one argument"))
            end
            if spec.action == :append
                push!(res[spec.dest], val)
            else
                res[spec.dest] = val
            end
        end
        i += 1
    end
    missing_ = [join(s.flags, "/") for s in p.specs if s.required && !(s.dest in seen)]
    isempty(missing_) || throw(ArgParseError("the following arguments are required: " * join(missing_, ", ")))
    return res
end

end # module MbjlUtil
