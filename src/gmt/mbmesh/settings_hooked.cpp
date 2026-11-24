// Upstream src/mbmesh/settings.cpp compiled here, unchanged, with its read_datalist() call routed to the fork's
// reader (input_hooks.h): the soundings from memory, or one swath file without a datalist.
#include "io/datalist_reader.h"
#include "input_hooks.h"

#define read_datalist mbmesh_program_read_datalist
#include "settings.cpp"
#undef read_datalist
