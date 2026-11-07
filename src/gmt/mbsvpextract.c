/*--------------------------------------------------------------------
 *    The MB-system:  mbsvpextract.c  4 November 2026
 *
 *    Copyright (c) 2026-2026 by
 *    David W. Caress (caress@mbari.org)
 *      Monterey Bay Aquarium Research Institute
 *      Moss Landing, California, USA
 *    Dale N. Chayes
 *      Center for Coastal and Ocean Mapping
 *      University of New Hampshire
 *      Durham, New Hampshire, USA
 *    Christian dos Santos Ferreira
 *      MARUM
 *      University of Bremen
 *      Bremen Germany
 *
 *    MB-System was created by Caress and Chayes in 1992 at the
 *      Lamont-Doherty Earth Observatory
 *      Columbia University
 *      Palisades, NY 10964
 *
 *    See README.md file for copying and redistribution conditions.
 *--------------------------------------------------------------------*/
/*
 * mbsvpextract extracts annual sound velocity profiles from a World Ocean
 * Atlas sound speed grid at the sites of a site file or at the waypoints of
 * a route file. The user names the grid by edition, period and resolution, so
 * that nothing depends on which datasets happen to be installed. Each position
 * takes the profile of the nearest grid cell that can be completed to 11000 m,
 * and each gets its own profile file, for each formula.
 *
 * A profile is a climatological candidate, not a forecast or a measurement
 * of the water column at that position.
 *
 * Author:  C. dos Santos Ferreira
 * Date:    November 4, 2026
 *
 * GMT-module port of src/svptools/mbsvpextract.c: the getopt_long loop is
 * replaced by the GMT option parser: the long options with a short letter
 * go through module_kw, the long-only ones through mb_gmt_mark_long_options(),
 * and the lower-case aliases are kept. The lines the program prints on stdout go
 * through the GMT API, and main() becomes GMT_mbsvpextract(), every exit()
 * a Return() with a GMT error code. The work is done by the mbsvptools
 * library, as in the program. The module runs in its caller's process: the
 * SIGINT/SIGTERM handlers it installs are restored before it returns, and
 * mbsvp_interrupted is cleared on entry and on return. The profile and
 * summary files are opened in text mode on Windows, where a GMT session
 * makes binary the default, so that they are written as the program writes
 * them.
 */

#define THIS_MODULE_NAME "mbsvpextract"
#define THIS_MODULE_LIB "mbsystem"
#define THIS_MODULE_PURPOSE "Extract World Ocean Atlas sound velocity profiles at sites or route waypoints"
/* Input is the site or route file given with -I; the lines naming each
 * position's profile files and the summary are listed to stdout. The
 * profile and summary files are written by the module itself. */
#define THIS_MODULE_KEYS "ID{,>D}"
#define THIS_MODULE_NEEDS ""
#define THIS_MODULE_OPTIONS "->V"

#include "gmt_dev.h"
#include "mb_gmt_compat.h"

#include <math.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "mb_define.h"
#include "mb_status.h"
#include "mb_gmt_opts.h"
#include "mb_gmt_text.h"
#include "mbsvp_tools.h"

/* A GMT session makes binary the default file mode on Windows: the program's text files */
#ifdef _WIN32
#define MBSVPEXTRACT_WRITE "wt"
#else
#define MBSVPEXTRACT_WRITE "w"
#endif

static const char program_name[] = "mbsvpextract";
static const char help_message[] =
    "mbsvpextract extracts annual sound velocity profiles from a World Ocean\n"
    "Atlas sound speed grid at the sites of a site file (.ste) or at the\n"
    "waypoints of a route file (.rte). The grid is named by edition, period\n"
    "and resolution. Each position takes the nearest grid cell whose profile\n"
    "can be completed to 11000 m. It requires one of the datasets posted at\n"
    MBSVP_DATASETS_URL;
static const char usage_message[] =
    "mbsvpextract\n"
    "\t--input=file {-Ifile} (or --sites=file or --route=file)\n"
    "\t--svpfolder=directory {-Sdirectory}\n"
    "\t--outputfolder=directory {-Odirectory}\n"
    "\t--woa-version=18|23\n"
    "\t--period=period {-Pperiod} (such as decav, A5B7 or B5C2)\n"
    "\t--resolution=01|04 {-R01|04}\n"
    "\t--formula=chen|leroy|both\n"
    "\t--strict-nearest\n"
    "\t--overwrite\n"
    "\t--help {-H}\n"
    "\t--verbose {-V}";

struct row {
  char status[16];
  char outputs[2][MB_PATH_MAXLINE];
  int noutputs;
  int formula[2];
  char metadata[MBSVP_COMMENT_LIMIT + 64];
  bool has_result;
  bool substituted;
  int rank;
  char error[MBSVP_MESSAGE_SIZE];
};

struct options {
  int verbose;
  mb_path input;
  enum mbsvp_input_type input_type;
  mb_path svpfolder;
  mb_path outputfolder;
  char edition[8];
  char period[64];
  char resolution[8];
  char formula[8];
  bool strict_nearest;
  bool overwrite;
};

static const char *formula_names[2] = {"chen", "leroy"};

/*--------------------------------------------------------------------*/
static void handle_signal(int signal_number) {
  (void)signal_number;
  mbsvp_interrupted = 1;
}
/*--------------------------------------------------------------------*/
/* The profile of the nearest completable cell. All wanted formulas must
   complete in the same cell. */
static int extract_point(int verbose, struct mbsvp_atlas *atlas, const struct mbsvp_point *point, const bool want[2],
                         bool strict, double *speed, struct mbsvp_profile profiles[2], int *rank, double *cell_lon,
                         double *cell_lat, bool *used_neighbour, double *physics_from, char *message, int *error) {
  int cell_lat_index[MBSVP_NEAREST_CELLS];
  int cell_lon_index[MBSVP_NEAREST_CELLS];
  int ncells = 0;
  if (mbsvp_woa_order_cells(verbose, atlas->lat, atlas->nlat, atlas->lon, atlas->nlon, point->latitude, point->longitude,
                            strict ? 1 : MBSVP_NEAREST_CELLS, cell_lat_index, cell_lon_index, &ncells, message,
                            error) != MB_SUCCESS)
    return MB_FAILURE;

  struct mbsvp_strings reasons;
  mbsvp_strings_init(&reasons);
  int status = MB_FAILURE;
  for (int c = 0; c < ncells && status != MB_SUCCESS; c++) {
    bool complete = true;
    *used_neighbour = false;
    *physics_from = MBSVP_TARGET_DEPTH;
    for (int v = 0; v < 2 && complete; v++) {
      if (!want[v])
        continue;
      char reason[MBSVP_MESSAGE_SIZE] = "";
      int local_error = MB_ERROR_NO_ERROR;
      if (mbsvp_atlas_read_cell(verbose, atlas, v, cell_lat_index[c], cell_lon_index[c], speed, reason, &local_error) !=
          MB_SUCCESS) {
        mbsvp_set_message(message, "%s", reason);
        *error = local_error;
        int error_free = MB_ERROR_NO_ERROR;
        mbsvp_strings_free(verbose, &reasons, &error_free);
        return MB_FAILURE;
      }
      const struct mbsvp_profile raw = {atlas->ndepth, atlas->depth, speed};
      struct mbsvp_profile extended;
      mbsvp_profile_init(&extended);
      bool neighbour = false;
      double from = MBSVP_TARGET_DEPTH;
      if (mbsvp_atlas_extend(verbose, atlas, v, cell_lat_index[c], cell_lon_index[c], &raw, &extended, &neighbour, reason, &local_error) !=
              MB_SUCCESS ||
          mbsvp_profile_complete(verbose, &extended, MBSVP_TARGET_DEPTH, v == 0 ? MBSVP_CHEN : MBSVP_LEROY, atlas->lat[cell_lat_index[c]],
                                 &profiles[v], &from, reason, &local_error) != MB_SUCCESS) {
        complete = false;
        if (!mbsvp_strings_contains(&reasons, reason))
          mbsvp_strings_add(verbose, &reasons, reason, error);
      }
      else {
        if (neighbour)
          *used_neighbour = true;
        if (from < *physics_from)
          *physics_from = from;
      }
      int error_extended = MB_ERROR_NO_ERROR;
      mbsvp_profile_free(verbose, &extended, &error_extended);
    }
    if (complete) {
      *rank = c;
      *cell_lon = mbsvp_normalize_longitude(atlas->lon[cell_lon_index[c]]);
      *cell_lat = atlas->lat[cell_lat_index[c]];
      status = MB_SUCCESS;
    }
  }
  if (status != MB_SUCCESS) {
    char joined[MBSVP_MESSAGE_SIZE] = "";
    size_t len = 0;
    for (int i = 0; i < reasons.n && len < sizeof(joined); i++)
      len += snprintf(&joined[len], sizeof(joined) - len, "%s%s", i > 0 ? "; " : "", reasons.items[i]);
    mbsvp_set_message(message, "No completable near-surface profile among %d nearest cells: %s", ncells, joined);
    *error = MB_ERROR_BAD_DATA;
  }
  int error_free = MB_ERROR_NO_ERROR;
  mbsvp_strings_free(verbose, &reasons, &error_free);
  return status;
}
/*--------------------------------------------------------------------*/
/* The JSON metadata of a profile, in one line. */
static bool make_metadata(const struct options *options, const struct mbsvp_point *point, double cell_lon, double cell_lat,
                          bool used_neighbour, double physics_from, char *metadata, size_t size) {
  char lon[64];
  char lat[64];
  char woa_lon[64];
  char woa_lat[64];
  mbsvp_format_double(point->longitude, lon, sizeof(lon));
  mbsvp_format_double(point->latitude, lat, sizeof(lat));
  mbsvp_format_double(cell_lon, woa_lon, sizeof(woa_lon));
  mbsvp_format_double(cell_lat, woa_lat, sizeof(woa_lat));
  int len = snprintf(metadata, size,
                     "{\"version\":1,\"Longitude_site\":%s,\"Latitude_site\":%s,\"WOAdataset\":\"WOA%s\","
                     "\"WOA_resolution\":\"%s\",\"WOA_period\":\"%s\",\"Longitude_WOA\":%s,\"Latitude_WOA\":%s",
                     lon, lat, options->edition, options->resolution, options->period, woa_lon, woa_lat);
  if (len < 0 || (size_t)len >= size)
    return false;
  if (point->is_waypoint) {
    char route[sizeof(point->route) * 2];
    mbsvp_json_escape(point->route, route, sizeof(route));
    const int added = snprintf(&metadata[len], size - len, ",\"Route\":\"%.160s\",\"Waypoint\":%d", route, point->waypoint);
    if (added < 0 || (size_t)added >= size - len)
      return false;
    len += added;
  }
  char from[64];
  mbsvp_format_double(physics_from, from, sizeof(from));
  const int added = snprintf(&metadata[len], size - len, ",\"Deeper_neighbour\":%s,\"Physics_from_depth\":%s}",
                             used_neighbour ? "true" : "false", from);
  return added > 0 && (size_t)added < size - len;
}
/*--------------------------------------------------------------------*/
static int write_profile(int verbose, const struct options *options, const struct mbsvp_point *point, int formula,
                         const char *metadata, const struct mbsvp_profile *profile, const char *path, char *message, int *error) {
  /* the first comment holds no control characters and at most 160 of the name */
  char label[161];
  size_t k = 0;
  for (const unsigned char *c = (const unsigned char *)point->name; *c != '\0' && k < sizeof(label) - 1; c++)
    label[k++] = (*c < 0x20 || *c == 0x7f) ? ' ' : (char)*c;
  label[k] = '\0';

  char temporary[MB_PATH_MAXLINE + 64];
  snprintf(temporary, sizeof(temporary), "%s.%d.tmp", path, mbsvp_process_id());
  FILE *fp = fopen(temporary, MBSVPEXTRACT_WRITE);
  if (fp == NULL) {
    mbsvp_set_message(message, "Cannot write %s", temporary);
    *error = MB_ERROR_OPEN_FAIL;
    return MB_FAILURE;
  }
  fprintf(fp, "# mbsvpextract; formula=%s; period=%s; annual average; endpoint=11000m; site=%s\n", formula_names[formula],
          options->period, label);
  fprintf(fp, "%s%s\n", MBSVP_MARKER_EXTRACT, metadata);
  for (int i = 0; i < profile->n; i++)
    fprintf(fp, "%.6f %.9f\n", profile->depth[i], profile->speed[i]);
  const bool bad = ferror(fp) != 0;
  if (fclose(fp) != 0 || bad) {
    remove(temporary);
    mbsvp_set_message(message, "Cannot write %s", temporary);
    *error = MB_ERROR_WRITE_FAIL;
    return MB_FAILURE;
  }
  if (mbsvp_commit_file(verbose, temporary, path, options->overwrite, error) != MB_SUCCESS) {
    mbsvp_set_message(message, "Cannot create %s", path);
    return MB_FAILURE;
  }
  return MB_SUCCESS;
}
/*--------------------------------------------------------------------*/
/* Written after every position, by replacing the file, so that a stopped run
   leaves a readable summary. */
static void write_summary(const char *summary, const char *created, const struct options *options, const char *dataset,
                          const struct mbsvp_points *points, const struct row *rows, int nrows, bool interrupted,
                          const char *fatal) {
  char temporary[MB_PATH_MAXLINE + 64];
  char text[2 * MBSVP_MESSAGE_SIZE];
  char number[64];
  snprintf(temporary, sizeof(temporary), "%s.%d.tmp", summary, mbsvp_process_id());
  FILE *fp = fopen(temporary, MBSVPEXTRACT_WRITE);
  if (fp == NULL)
    return;
  fprintf(fp, "{\n  \"tool\": \"%s\",\n  \"created_utc\": \"%s\",\n", program_name, created);
  mbsvp_json_escape(options->input, text, sizeof(text));
  fprintf(fp, "  \"input_file\": \"%s\",\n  \"input_type\": \"%s\",\n", text,
          options->input_type == MBSVP_INPUT_ROUTE ? "route" : "sites");
  mbsvp_json_escape(dataset, text, sizeof(text));
  fprintf(fp, "  \"dataset\": \"%s\",\n  \"edition\": \"WOA%s\",\n  \"period\": \"%s\",\n", text, options->edition, options->period);
  fprintf(fp, "  \"resolution\": \"%s\",\n  \"formula\": \"%s\",\n  \"strict_nearest\": %s,\n", options->resolution, options->formula,
          options->strict_nearest ? "true" : "false");
  fprintf(fp, "  \"endpoint_metres\": 11000,\n  \"sites\": [");
  for (int i = 0; i < nrows; i++) {
    const struct mbsvp_point *point = &points->items[i];
    const struct row *row = &rows[i];
    mbsvp_json_escape(point->name, text, sizeof(text));
    fprintf(fp, "%s\n    {\n      \"name\": \"%s\",\n", i > 0 ? "," : "", text);
    mbsvp_format_double(point->longitude, number, sizeof(number));
    fprintf(fp, "      \"longitude\": %s,\n", number);
    mbsvp_format_double(point->latitude, number, sizeof(number));
    fprintf(fp, "      \"latitude\": %s,\n      \"line\": %d,\n", number, point->line);
    if (point->is_waypoint) {
      mbsvp_json_escape(point->route, text, sizeof(text));
      fprintf(fp, "      \"route\": \"%s\",\n      \"waypoint\": %d,\n      \"waypoint_type\": %d,\n", text, point->waypoint,
              point->waypoint_type);
    }
    fprintf(fp, "      \"status\": \"%s\",\n      \"outputs\": [", row->status);
    for (int k = 0; k < row->noutputs; k++) {
      mbsvp_json_escape(row->outputs[k], text, sizeof(text));
      fprintf(fp, "%s{\"file\": \"%s\", \"formula\": \"%s\"}", k > 0 ? ", " : "", text, formula_names[row->formula[k]]);
    }
    fprintf(fp, "]");
    if (row->has_result)
      fprintf(fp, ",\n      \"metadata\": %s,\n      \"substituted\": %s,\n      \"candidate_rank\": %d", row->metadata,
              row->substituted ? "true" : "false", row->rank + 1);
    if (row->error[0] != '\0') {
      mbsvp_json_escape(row->error, text, sizeof(text));
      fprintf(fp, ",\n      \"error\": \"%s\"", text);
    }
    fprintf(fp, "\n    }");
  }
  fprintf(fp, "%s],\n", nrows > 0 ? "\n  " : "");
  if (fatal != NULL && fatal[0] != '\0') {
    mbsvp_json_escape(fatal, text, sizeof(text));
    fprintf(fp, "  \"error\": \"%s\",\n", text);
  }
  fprintf(fp, "  \"interrupted\": %s\n}\n", interrupted ? "true" : "false");
  if (fclose(fp) != 0) {
    remove(temporary);
    return;
  }
#ifdef _WIN32
  remove(summary);
#endif
  rename(temporary, summary);
}
/*--------------------------------------------------------------------*/
/* Process one point. A failure is recorded and the run goes on. The line the
   program prints on stdout goes to T. */
static void process_point(const struct options *options, struct mbsvp_atlas *atlas, const struct mbsvp_point *point,
                          const char *folder, const bool want[2], double *speed, struct row *row, struct MB_GMT_TEXT *T) {
  const int verbose = options->verbose;
  int error = MB_ERROR_NO_ERROR;
  const int nwanted = (want[0] ? 1 : 0) + (want[1] ? 1 : 0);
  mb_path paths[2];
  size_t len = 0;
  char existing[MBSVP_MESSAGE_SIZE] = "";
  for (int v = 0; v < 2; v++) {
    if (!want[v])
      continue;
    const int nchar = snprintf(paths[v], sizeof(paths[v]), "%s/%s%s%s.svp", folder, point->base, nwanted > 1 ? "_" : "",
                               nwanted > 1 ? formula_names[v] : "");
    if (nchar < 0 || (size_t)nchar >= sizeof(paths[v])) {
      strcpy(row->status, "failed");
      mbsvp_set_message(row->error, "Output path is too long");
      return;
    }
    if (!options->overwrite && mbsvp_is_file(paths[v]) && len < sizeof(existing))
      len += snprintf(&existing[len], sizeof(existing) - len, "%s%s", len > 0 ? ", " : "", paths[v]);
  }
  if (len > 0) {
    strcpy(row->status, "failed");
    mbsvp_set_message(row->error, "Output exists; use --overwrite to replace: %s", existing);
    fprintf(stderr, "%s: %s\n", point->name, row->error);
    return;
  }

  struct mbsvp_profile profiles[2];
  mbsvp_profile_init(&profiles[0]);
  mbsvp_profile_init(&profiles[1]);
  double cell_lon = 0.0;
  double cell_lat = 0.0;
  int rank = 0;
  bool used_neighbour = false;
  double physics_from = MBSVP_TARGET_DEPTH;
  int status = extract_point(verbose, atlas, point, want, options->strict_nearest, speed, profiles, &rank, &cell_lon, &cell_lat,
                             &used_neighbour, &physics_from, row->error, &error);
  if (status == MB_SUCCESS &&
      !make_metadata(options, point, cell_lon, cell_lat, used_neighbour, physics_from, row->metadata, sizeof(row->metadata))) {
    mbsvp_set_message(row->error, "Profile comment is too long");
    status = MB_FAILURE;
  }
  for (int v = 0; v < 2 && status == MB_SUCCESS; v++) {
    if (!want[v])
      continue;
    status = write_profile(verbose, options, point, v, row->metadata, &profiles[v], paths[v], row->error, &error);
    if (status == MB_SUCCESS) {
      snprintf(row->outputs[row->noutputs], sizeof(row->outputs[row->noutputs]), "%s", paths[v]);
      row->formula[row->noutputs] = v;
      row->noutputs++;
    }
  }
  if (status == MB_SUCCESS) {
    if (rank > 0)
      fprintf(stderr, "Warning: %s: nearest cell invalid; using valid candidate #%d\n", point->name, rank + 1);
    strcpy(row->status, "success");
    row->has_result = true;
    row->substituted = rank > 0;
    row->rank = rank;
    mb_gmt_text_put(T, "%s:", point->name);
    for (int k = 0; k < row->noutputs; k++)
      mb_gmt_text_put(T, "%s %s", k > 0 ? "," : "", row->outputs[k]);
    mb_gmt_text_put(T, "\n");
  }
  else {
    strcpy(row->status, "failed");
    fprintf(stderr, "%s: %s\n", point->name, row->error);
  }
  int error_free = MB_ERROR_NO_ERROR;
  mbsvp_profile_free(verbose, &profiles[0], &error_free);
  mbsvp_profile_free(verbose, &profiles[1], &error_free);
}
/*--------------------------------------------------------------------*/
/* The program's run(): its exit code, 0, 1 or 130 when interrupted */
static int run(const struct options *options_in, struct MB_GMT_TEXT *T) {
  struct options options = *options_in;
  const int verbose = options.verbose;
  int error = MB_ERROR_NO_ERROR;
  char message[MBSVP_MESSAGE_SIZE] = "";

  if (!mbsvp_is_file(options.input)) {
    fprintf(stderr, "Input does not exist: %s\n", options.input);
    return 1;
  }
  if (options.input_type == MBSVP_INPUT_AUTO && mbsvp_points_detect(verbose, options.input, &options.input_type, message, &error) != MB_SUCCESS) {
    fprintf(stderr, "%s\n", message);
    return 1;
  }

  struct mbsvp_points points;
  struct mbsvp_strings notes;
  mbsvp_points_init(&points);
  mbsvp_strings_init(&notes);
  struct row *rows = NULL;
  double *speed = NULL;
  struct mbsvp_atlas atlas;
  mbsvp_atlas_init(&atlas);
  int exit_code = 1;
  bool have_summary = false;
  mb_path summary = "";
  char created[32] = "";
  mb_path dataset = "";
  int nrows = 0;

  if (mbsvp_points_read(verbose, options.input, options.input_type, &points, &notes, message, &error) != MB_SUCCESS)
    goto done;
  mbsvp_points_make_bases(&points);
  for (int i = 0; i < notes.n; i++)
    fprintf(stderr, "Note: %s\n", notes.items[i]);

  char name[256];
  const int name_len = snprintf(name, sizeof(name), "woa%s_%s_svp00_%s.nc", options.edition, options.period, options.resolution);
  if (name_len < 0 || (size_t)name_len >= sizeof(name) || !mbsvp_join(dataset, sizeof(dataset), options.svpfolder, name)) {
    mbsvp_set_message(message, "Dataset path is too long");
    goto done;
  }
  if (!mbsvp_is_file(dataset)) {
    mbsvp_set_message(message, "Required dataset not found: %s\nDatasets are posted at %s", dataset, MBSVP_DATASETS_URL);
    goto done;
  }

  mb_path folder;
  if (mbsvp_make_dirs(verbose, options.outputfolder, &error) != MB_SUCCESS ||
      mbsvp_absolute_path(verbose, options.outputfolder, folder, sizeof(folder), &error) != MB_SUCCESS) {
    mbsvp_set_message(message, "Cannot make output directory: %s", options.outputfolder);
    goto done;
  }
  mb_path absolute;
  if (mbsvp_absolute_path(verbose, options.input, absolute, sizeof(absolute), &error) == MB_SUCCESS)
    strcpy(options.input, absolute);

  /* a new summary for each run: an earlier run keeps its record */
  const time_t now = time(NULL);
  const struct tm *utc = gmtime(&now);
  char stamp[32] = "00000000-000000";
  if (utc != NULL) {
    strftime(created, sizeof(created), "%Y-%m-%dT%H:%M:%S+00:00", utc);
    strftime(stamp, sizeof(stamp), "%Y%m%d-%H%M%S", utc);
  }
  for (int attempt = 0; attempt < 10000; attempt++) {
    int len;
    if (attempt == 0)
      len = snprintf(summary, sizeof(summary), "%s/mbsvpextract-%s.json", folder, stamp);
    else
      len = snprintf(summary, sizeof(summary), "%s/mbsvpextract-%s-%d.json", folder, stamp, attempt);
    if (len < 0 || (size_t)len >= sizeof(summary)) {
      mbsvp_set_message(message, "Output path is too long");
      goto done;
    }
    if (!mbsvp_is_file(summary))
      break;
  }

  if (mb_mallocd(verbose, __FILE__, __LINE__, points.n * sizeof(struct row), (void **)&rows, &error) != MB_SUCCESS) {
    mbsvp_set_message(message, "Out of memory");
    goto done;
  }
  have_summary = true;
  write_summary(summary, created, &options, dataset, &points, rows, 0, false, NULL);

  const bool want[2] = {strcmp(options.formula, "leroy") != 0, strcmp(options.formula, "chen") != 0};
  if (mbsvp_atlas_open(verbose, dataset, want, &atlas, message, &error) != MB_SUCCESS)
    goto done;
  if (mb_mallocd(verbose, __FILE__, __LINE__, atlas.ndepth * sizeof(double), (void **)&speed, &error) != MB_SUCCESS) {
    mbsvp_set_message(message, "Out of memory");
    goto done;
  }

  bool any_failed = false;
  for (int i = 0; i < points.n && !mbsvp_interrupted; i++) {
    struct row *row = &rows[i];
    memset(row, 0, sizeof(struct row));
    strcpy(row->status, "running");
    nrows = i + 1;
    write_summary(summary, created, &options, dataset, &points, rows, nrows, false, NULL);
    process_point(&options, &atlas, &points.items[i], folder, want, speed, row, T);
    if (strcmp(row->status, "success") != 0)
      any_failed = true;
    write_summary(summary, created, &options, dataset, &points, rows, nrows, false, NULL);
  }
  if (mbsvp_interrupted) {
    write_summary(summary, created, &options, dataset, &points, rows, nrows, true, NULL);
    exit_code = 130;
  }
  else {
    exit_code = any_failed ? 1 : 0;
  }
  mb_gmt_text_put(T, "Summary: %s\n", summary);

done:
  if (exit_code == 1 && message[0] != '\0') {
    fprintf(stderr, "%s\n", message);
    if (have_summary)
      write_summary(summary, created, &options, dataset, &points, rows, nrows, false, message);
  }
  int error_free = MB_ERROR_NO_ERROR;
  mbsvp_atlas_close(verbose, &atlas, &error_free);
  mbsvp_points_free(verbose, &points, &error_free);
  mbsvp_strings_free(verbose, &notes, &error_free);
  if (rows != NULL)
    mb_freed(verbose, __FILE__, __LINE__, (void **)&rows, &error_free);
  if (speed != NULL)
    mb_freed(verbose, __FILE__, __LINE__, (void **)&speed, &error_free);
  return exit_code;
}

/* --- Control structure ---------------------------------------------- */

struct MBSVPEXTRACT_CTRL {
	struct options options;		/* the program's options, filled in command-line order */
	struct mbse_H { bool active; } H;
	struct mbse_U { bool active; char arg[MB_PATH_MAXLINE]; } U;	/* the first unexpected argument */
};

static void *New_mbsvpextract_Ctrl(struct GMT_CTRL *GMT) {
	struct MBSVPEXTRACT_CTRL *Ctrl = gmt_M_memory(GMT, NULL, 1, struct MBSVPEXTRACT_CTRL);
	strcpy(Ctrl->options.formula, "chen");
	return Ctrl;
}

static void Free_mbsvpextract_Ctrl(struct GMT_CTRL *GMT, struct MBSVPEXTRACT_CTRL *Ctrl) {
	if (!Ctrl) return;
	gmt_M_free(GMT, Ctrl);
}

/* Translation table from the program's long options to its short ones */
static struct GMT_KEYWORD_DICTIONARY module_kw[] = {
	/* separator, short_option, long_option, short_directives, long_directives, short_modifiers, long_modifiers, transproc_mask */
	{ 0, 'H', "help",         "", "", "", "", GMT_TP_STANDARD },
	{ 0, 'I', "input",        "", "", "", "", GMT_TP_STANDARD },
	{ 0, 'O', "outputfolder", "", "", "", "", GMT_TP_STANDARD },
	{ 0, 'P', "period",       "", "", "", "", GMT_TP_STANDARD },
	{ 0, 'R', "resolution",   "", "", "", "", GMT_TP_STANDARD },
	{ 0, 'S', "svpfolder",    "", "", "", "", GMT_TP_STANDARD },
	{ 0, 'v', "verbose",      "", "", "", "", GMT_TP_STANDARD },
	{ 0, '\0', "", "", "", "", "", 0 }  /* End of list marked with empty option and strings */
};

/* The program's long options that have no short letter */
static const struct MB_GMT_LONGOPT_DEF long_options[] = {
	{"formula", true},
	{"overwrite", false},
	{"route", true},
	{"sites", true},
	{"strict-nearest", false},
	{"woa-version", true},
	{NULL, false}
};

static int usage(struct GMTAPI_CTRL *API, int level) {
	gmt_show_name_and_purpose(API, THIS_MODULE_LIB, THIS_MODULE_NAME, THIS_MODULE_PURPOSE);
	if (level == GMT_MODULE_PURPOSE) return GMT_NOERROR;
	GMT_Message(API, GMT_TIME_NONE, "usage: %s\n", usage_message);
	if (level == GMT_SYNOPSIS) return GMT_MODULE_SYNOPSIS;
	GMT_Message(API, GMT_TIME_NONE, "\n%s\n\n", help_message);
	GMT_Message(API, GMT_TIME_NONE,
	            "\t-I Input site file [.ste] or route file [.rte], its type told by its header line,\n"
	            "\t   else by its suffix; --sites=file and --route=file force the type.\n"
	            "\t-S Directory holding the World Ocean Atlas sound speed grids.\n"
	            "\t-O Output directory for the profiles and the run summary; made when missing.\n"
	            "\t--woa-version=18|23 World Ocean Atlas edition.\n"
	            "\t-P Period of the grid, such as decav, A5B7 or B5C2.\n"
	            "\t-R Resolution of the grid: 01 or 04 degrees.\n"
	            "\t--formula=chen|leroy|both Sound speed formula [chen].\n"
	            "\t--strict-nearest Use only the nearest grid cell.\n"
	            "\t--overwrite Replace existing profile files.\n"
	            "\t-H Print help and exit.\n"
	            "\tEvery option also has the program's lower-case and long forms.\n");
	GMT_Option(API, "V,.");
	return GMT_MODULE_USAGE;
}

static int parse_mbsvpextract(struct GMT_CTRL *GMT, struct MBSVPEXTRACT_CTRL *Ctrl, struct GMT_OPTION *options) {
	struct GMTAPI_CTRL *API = GMT->parent;
	struct options *o = &Ctrl->options;
	unsigned int n_errors = 0;
	struct GMT_OPTION *opt;
	for (opt = options; opt; opt = opt->next) {
		if (opt->option == MB_GMT_LONGOPT) {	/* a long option with no short letter */
			const char *value;
			const int k = mb_gmt_long_option(opt, long_options, &value);
			if (k < 0) {
				GMT_Report(API, GMT_MSG_ERROR, "Option --%s %s\n", opt->arg, k == -2 ? "requires an argument" : "is not recognized");
				n_errors++;
				continue;
			}
			const char *name = long_options[k].name;
			if (strcmp("formula", name) == 0) {
				snprintf(o->formula, sizeof(o->formula), "%s", value);
			}
			else if (strcmp("overwrite", name) == 0) {
				o->overwrite = true;
			}
			else if (strcmp("route", name) == 0 || strcmp("sites", name) == 0) {
				snprintf(o->input, sizeof(o->input), "%s", value);
				o->input_type = strcmp("route", name) == 0 ? MBSVP_INPUT_ROUTE : MBSVP_INPUT_SITES;
			}
			else if (strcmp("strict-nearest", name) == 0) {
				o->strict_nearest = true;
			}
			else if (strcmp("woa-version", name) == 0) {
				snprintf(o->edition, sizeof(o->edition), "%s", value);
			}
			continue;
		}
		switch (opt->option) {
		case 'V': case 'v':
			o->verbose++;
			break;
		case 'H': case 'h':
			Ctrl->H.active = true;
			break;
		case 'I': case 'i':
			if (opt->arg && opt->arg[0]) {
				snprintf(o->input, sizeof(o->input), "%s", opt->arg);
				o->input_type = MBSVP_INPUT_AUTO;
			}
			else n_errors++;
			break;
		case 'O': case 'o':
			if (opt->arg && opt->arg[0]) snprintf(o->outputfolder, sizeof(o->outputfolder), "%s", opt->arg);
			else n_errors++;
			break;
		case 'P': case 'p':
			if (opt->arg && opt->arg[0]) snprintf(o->period, sizeof(o->period), "%s", opt->arg);
			else n_errors++;
			break;
		case 'R': case 'r':
			if (opt->arg && opt->arg[0]) snprintf(o->resolution, sizeof(o->resolution), "%s", opt->arg);
			else n_errors++;
			break;
		case 'S': case 's':
			if (opt->arg && opt->arg[0]) snprintf(o->svpfolder, sizeof(o->svpfolder), "%s", opt->arg);
			else n_errors++;
			break;
		case GMT_OPT_INFILE:	/* the program's "Unexpected argument", reported with the other usage errors */
			if (!Ctrl->U.active) {
				Ctrl->U.active = true;
				snprintf(Ctrl->U.arg, sizeof(Ctrl->U.arg), "%s", opt->arg ? opt->arg : "");
			}
			break;
		default:
			n_errors += gmt_default_option_error(GMT, opt);
			break;
		}
	}
	return n_errors ? GMT_PARSE_ERROR : GMT_NOERROR;
}

/* The handlers that were in place before the module installed its own */
struct mbsvpextract_signals {
	bool set;
	void (*sigint)(int);
	void (*sigterm)(int);
};

static void restore_signals(struct mbsvpextract_signals *saved) {
	if (saved->set) {
		if (saved->sigint != SIG_ERR) signal(SIGINT, saved->sigint);
		if (saved->sigterm != SIG_ERR) signal(SIGTERM, saved->sigterm);
		saved->set = false;
	}
	mbsvp_interrupted = 0;
}

#define bailout(code) { gmt_M_free_options(mode); return (code); }
#define Return(code) { restore_signals(&saved); if (T) mb_gmt_text_end(T); Free_mbsvpextract_Ctrl(GMT, Ctrl); gmt_end_module(GMT, GMT_cpy); bailout(code); }
EXTERN_MSC int GMT_mbsvpextract(void *V_API, int mode, void *args);

/*--------------------------------------------------------------------*/

int GMT_mbsvpextract(void *V_API, int mode, void *args) {
	struct GMTAPI_CTRL *API = gmt_get_api_ptr(V_API);
	struct GMT_CTRL *GMT = NULL, *GMT_cpy = NULL;
	struct GMT_OPTION *options = NULL;
	struct MBSVPEXTRACT_CTRL *Ctrl = NULL;
	struct MB_GMT_TEXT *T = NULL;	/* the lines the program prints on stdout, through the GMT API */
	struct mbsvpextract_signals saved = {false, SIG_DFL, SIG_DFL};
	int parse_error;

	if (!API) return GMT_NOT_A_SESSION;
	if (mode == GMT_MODULE_PURPOSE) return usage(API, GMT_MODULE_PURPOSE);
	options = GMT_Create_Options(API, mode, args);
	if (API->error) return API->error;
	/* 0: the program cannot run without options */
	if ((parse_error = gmt_report_usage(API, options, 0, usage)) != GMT_NOERROR) bailout(parse_error);
	/* the program's long options with no short letter, kept out of the keyword translation and of
	   GMT's --PAR=value handling */
	mb_gmt_mark_long_options(API, &options, long_options);
	if ((GMT = gmt_init_module(API, THIS_MODULE_LIB, THIS_MODULE_NAME, THIS_MODULE_KEYS,
	                           THIS_MODULE_NEEDS, module_kw, &options, &GMT_cpy)) == NULL) bailout(API->error);
	/* getopt's "-I file" form, as the program's tests use it */
	mb_gmt_join_separated_values(API, &options, "IiOoPpRrSs");
	if (GMT_Parse_Common(API, THIS_MODULE_OPTIONS, options)) Return(API->error);
	Ctrl = (struct MBSVPEXTRACT_CTRL *)New_mbsvpextract_Ctrl(GMT);
	if ((parse_error = parse_mbsvpextract(GMT, Ctrl, options)) != GMT_NOERROR) Return(parse_error);

	struct options *o = &Ctrl->options;
	const int verbose = o->verbose;

	if (Ctrl->H.active || verbose >= 1) {
		fprintf(stderr, "\nProgram %s\n", program_name);
		fprintf(stderr, "MB-system Version %s\n", MB_VERSION);
	}
	if (Ctrl->H.active) Return(usage(API, GMT_USAGE));

	/* a stop requested in an earlier run of this session is not this run's */
	mbsvp_interrupted = 0;

	bool errflg = false;
	if (Ctrl->U.active) {
		fprintf(stderr, "Unexpected argument: %s\n", Ctrl->U.arg);
		errflg = true;
	}
	const struct {
		const char *value;
		const char *text;
	} required[] = {{o->input, "--input {-I}, --sites or --route"},
	                {o->svpfolder, "--svpfolder {-S}"},
	                {o->outputfolder, "--outputfolder {-O}"},
	                {o->edition, "--woa-version"
	                             " (18 or 23)"},
	                {o->period, "--period {-P} (such as decav, A5B7 or B5C2)"},
	                {o->resolution, "--resolution {-R} (01 or 04)"}};
	for (size_t i = 0; i < sizeof(required) / sizeof(required[0]); i++) {
		if (required[i].value[0] == '\0') {
			fprintf(stderr, "%s is required\n", required[i].text);
			errflg = true;
		}
	}
	if (o->edition[0] != '\0' && strcmp(o->edition, "18") != 0 && strcmp(o->edition, "23") != 0) {
		fprintf(stderr, "--woa-version must be 18 or 23\n");
		errflg = true;
	}
	if (o->resolution[0] != '\0' && strcmp(o->resolution, "01") != 0 && strcmp(o->resolution, "04") != 0) {
		fprintf(stderr, "--resolution must be 01 or 04\n");
		errflg = true;
	}
	if (strcmp(o->formula, "chen") != 0 && strcmp(o->formula, "leroy") != 0 && strcmp(o->formula, "both") != 0) {
		fprintf(stderr, "--formula must be chen, leroy or both\n");
		errflg = true;
	}
	for (const char *p = o->period; *p != '\0'; p++) {
		if (!((*p >= 'A' && *p <= 'Z') || (*p >= 'a' && *p <= 'z') || (*p >= '0' && *p <= '9'))) {
			fprintf(stderr, "--period holds letters and digits only\n");
			errflg = true;
			break;
		}
	}
	if (errflg) {
		fprintf(stderr, "usage: %s\n", usage_message);
		fprintf(stderr, "\nProgram <%s> Terminated\n", program_name);
		/* the program exits with MB_ERROR_BAD_USAGE */
		Return(GMT_PARSE_ERROR);
	}

	if (verbose >= 2) {
		fprintf(stderr, "\ndbg2  Program <%s>\n", program_name);
		fprintf(stderr, "dbg2  Control Parameters:\n");
		fprintf(stderr, "dbg2       verbose:        %d\n", verbose);
		fprintf(stderr, "dbg2       input:          %s\n", o->input);
		fprintf(stderr, "dbg2       input_type:     %d\n", o->input_type);
		fprintf(stderr, "dbg2       svpfolder:      %s\n", o->svpfolder);
		fprintf(stderr, "dbg2       outputfolder:   %s\n", o->outputfolder);
		fprintf(stderr, "dbg2       edition:        %s\n", o->edition);
		fprintf(stderr, "dbg2       period:         %s\n", o->period);
		fprintf(stderr, "dbg2       resolution:     %s\n", o->resolution);
		fprintf(stderr, "dbg2       formula:        %s\n", o->formula);
		fprintf(stderr, "dbg2       strict_nearest: %d\n", o->strict_nearest);
		fprintf(stderr, "dbg2       overwrite:      %d\n", o->overwrite);
	}

	/* the "name: files" and "Summary:" lines: text records through the GMT API */
	if ((T = mb_gmt_text_begin(GMT, options)) == NULL) Return(API->error);

	/* the caller's handlers are put back by Return() */
	saved.sigint = signal(SIGINT, handle_signal);
	saved.sigterm = signal(SIGTERM, handle_signal);
	saved.set = true;

	const int exit_code = run(o, T);

	restore_signals(&saved);

	if (verbose >= 4) {
		int error_free = MB_ERROR_NO_ERROR;
		mb_memory_list(verbose, &error_free);
	}
	if (verbose >= 2) {
		fprintf(stderr, "\ndbg2  Program <%s> completed\n", program_name);
		fprintf(stderr, "dbg2       exit code:      %d\n", exit_code);
	}

	const int output_failed = mb_gmt_text_end(T);
	T = NULL;
	if (output_failed) Return(GMT_RUNTIME_ERROR);

	/* The program exits with 1 when a position or the run failed and 130 when it was
	   interrupted; as a module those are GMT error codes */
	if (exit_code != 0) Return(GMT_RUNTIME_ERROR);
	Return(GMT_NOERROR);
}
/*--------------------------------------------------------------------*/
