// Upstream src/mbmesh/src/io/datalist_reader.cpp compiled here, unchanged, so that the reader below can use its
// internal functions (the reading of one swath file, the accepting of one sounding) without a datalist file.
#include "src/io/datalist_reader.cpp"

#include "input_hooks.h"
#include "mesh_capture.h"

#include <limits>

namespace {

// The end of upstream read_datalist(), as it is there: the frame from the centroid, the points and the track in it.
bool finish_reading(ReadContext &context, DatalistReadResult &read_result, const std::string &label,
                    DatalistReadResult *result, std::string *error) {
    if (read_result.stats.soundings_accepted == 0) {
        set_error(error, "no accepted bathymetry soundings in " + label);
        return false;
    }
    if (context.spilling) {
        if (!context.spill.finish(error)) {
            return false;
        }
        read_result.spilled = true;
        read_result.spill_path = context.spill.path();
    }
    const double count = static_cast<double>(read_result.stats.soundings_accepted);
    const Vec3 centroid_ecef = context.reference_ecef + context.offset_sum * (1.0 / count);
    GeodeticPosition origin = ecef_to_geodetic(centroid_ecef);
    origin.elevation = 0.0;
    constexpr double origin_resolution_degrees = 1.0e-6;
    origin.longitude = std::round(origin.longitude / origin_resolution_degrees) * origin_resolution_degrees;
    origin.latitude = std::round(origin.latitude / origin_resolution_degrees) * origin_resolution_degrees;
    read_result.frame = coordinate_frame_from_origin(origin);
    for (CollectedPoint &collected_point : read_result.points) {
        collected_point.point = ecef_to_local(collected_point.point, read_result.frame);
        collected_point.origin = ecef_to_local(collected_point.origin, read_result.frame);
    }
    for (TrackPoint &track_point : read_result.track) {
        track_point.position = ecef_to_local(track_point.position, read_result.frame);
    }
    std::sort(read_result.formats.begin(), read_result.formats.end());
    *result = std::move(read_result);
    return true;
}

}  // namespace

bool mbmesh_program_read_datalist(const std::filesystem::path &path, const DatalistReaderOptions &options_in,
                                  DatalistReadResult *result, std::string *error) {
    std::size_t npoints = 0;
    const double *points = mbmesh_capture_points(&npoints);
    int format = 0;
    if (points == nullptr) {
        std::string name = path.string();
        int format_error = MB_ERROR_NO_ERROR;
        if (name.size() < MB_PATH_MAXLINE) {
            mb_get_format(0, name.data(), nullptr, &format, &format_error);
        }
        if (format <= 0) {   // a datalist (format -1), or a name that says nothing: upstream's reader
            return read_datalist(path, options_in, result, error);
        }
    }
    if (result == nullptr) {
        set_error(error, "Datalist output result must not be null");
        return false;
    }
    DatalistReaderOptions options = options_in;
    if (mbmesh_capture_active()) {
        options.spill_points = std::numeric_limits<std::size_t>::max();   // in memory: nothing goes to disk
    }
    DatalistReadResult read_result;
    ReadContext context{options, read_result};
    std::string label;
    if (points != nullptr) {
        // As MBIO's generic lon lat topography format (168) gives them: one ping per point, the sensor at sea level
        // above it, no time.
        label = "the points handed over";
        read_result.stats.files_seen = 1;
        for (std::size_t i = 0; i < npoints; i++) {
            context.ping_recorded = false;
            Ping ping;
            ping.timestamp = 0.0;
            ping.sensor_position = {points[3 * i], points[3 * i + 1], 0.0};
            Sounding sounding;
            sounding.position = {points[3 * i], points[3 * i + 1], points[3 * i + 2]};
            sounding.beam_flag = MB_FLAG_NONE;
            ++read_result.stats.pings_read;
            if (!append_sounding_to_pointcloud(ping, sounding, &context, error)) {
                return false;
            }
        }
        read_result.files.push_back(label);
        read_result.formats.push_back(168);
        ++read_result.stats.files_read;
    }
    else {
        label = path.string();
        read_result.stats.files_seen = 1;
        std::string file = path.string();
        mb_path alternate_navigation_path = {};
        if (!read_swath_file(file.data(), format, MB_ALTNAV_NONE, alternate_navigation_path, &context, error)) {
            return false;
        }
        std::error_code path_error;
        const std::filesystem::path absolute_file = std::filesystem::absolute(path, path_error);
        read_result.files.push_back(path_error ? file : absolute_file.lexically_normal().string());
        read_result.formats.push_back(format);
        ++read_result.stats.files_read;
    }
    return finish_reading(context, read_result, label, result, error);
}
