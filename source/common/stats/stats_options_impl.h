#pragma once

#include <cstdint>

#include "envoy/stats/stats_options.h"

namespace Envoy {
namespace Stats {

// The max name length is based on current set of stats.
// As of now, the longest stat is
// cluster.<cluster_name>.outlier_detection.ejections_consecutive_5xx
// which is 52 characters long without the cluster name.
// The max stat name length is 127 (default). So, in order to give room
// for growth to both the envoy generated stat characters
// (e.g., outlier_detection...) and user supplied names (e.g., cluster name),
// we set the max user supplied name length to 60, and the max internally
// generated stat suffixes to 67 (15 more characters to grow).
// If you want to increase the max user supplied name length, use the compiler
// option ENVOY_DEFAULT_MAX_OBJ_NAME_LENGTH or the CLI option
// max-obj-name-len
struct StatsOptionsImpl : public StatsOptions {
  size_t maxNameLength() const override { return max_obj_name_length_ + max_stat_suffix_length_; }
  size_t maxObjNameLength() const override { return max_obj_name_length_; }
  size_t maxStatSuffixLength() const override { return max_stat_suffix_length_; }

  size_t max_obj_name_length_ = 60;
  size_t max_stat_suffix_length_ = 67;
};

} // namespace Stats
} // namespace Envoy
