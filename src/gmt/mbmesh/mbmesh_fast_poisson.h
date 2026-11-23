#pragma once

// The fork's fast Poisson solver for mbmesh (not part of upstream MB-System).
//
// screened_poisson_fast.cpp is compiled into mbmesh_pipeline (src/gmt/mbmesh) in place of upstream's
// src/algorithms/screened_poisson.cpp: it is that file, unchanged, plus a third solver, an octree
// multigrid (poisson_multigrid.h), chosen with --poisson-solver multigrid. Upstream's enum has
// no name for it, so it is the value after Sor. The GMT module runs mbmesh with it unless asked for
// another solver (mbmesh_capi.cpp).
//
// When upstream changes screened_poisson.cpp, CMake warns (its SHA-256 is checked against the one
// this copy was made from): merge the change into src/gmt/mbmesh/screened_poisson_fast.cpp.

#include "algorithms/screened_poisson.h"
#include "poisson_multigrid.h"

inline constexpr PoissonSolver kPoissonSolverMultigrid = static_cast<PoissonSolver>(2);

// The multigrid solves the finest grid only near the samples. Mesh is kept, by support trimming
// and void filling, up to half --void-size from a sounding, which the solver cannot see in its
// options: the host passes it here (meters; the default is that of --void-size 2, 1 m).
void mbmesh_fast_poisson_set_min_reach(double meters);

// The report of the last multigrid solve made on the calling thread.
const MultigridReport &mbmesh_fast_poisson_last_report();
