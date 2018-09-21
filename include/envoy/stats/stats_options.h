#pragma once

#include <memory>
#include <regex>

#include "envoy/common/pure.h"

#include "absl/types/optional.h"

namespace Envoy {
namespace Stats {

/**
 * Struct stored under Server::Options to hold information about the maximum
 * object name length and maximum stat suffix length of a stat. These have
 * defaults in StatsOptionsImpl, and the maximum object name length can be
 * overridden. The default initialization is used in IsolatedStatImpl, and the
 * user-overridden struct is stored in Options.
 *
 * As noted in the comment above StatsOptionsImpl in
 * source/common/stats/stats_options_impl.h, a stat name often contains both a
 * string whose length is user-defined (cluster_name in the below example), and
 * a specific statistic name generated by Envoy. To make room for growth on both
 * fronts, we limit the max allowed length of each separately.
 *
 *                           name / stat name
 *  |----------------------------------------------------------------|
 *  cluster.<cluster_name>.outlier_detection.ejections_consecutive_5xx
 *  |--------------------------------------| |-----------------------|
 *                object name                         suffix
 */
class StatsOptions {
public:
  virtual ~StatsOptions() {}

  /**
   * The max allowed length of a complete stat name, including suffix.
   */
  virtual size_t maxNameLength() const PURE;

  /**
   * The max allowed length of the object part of a stat name.
   */
  virtual size_t maxObjNameLength() const PURE;

  /**
   * The max allowed length of a stat suffix.
   */
  virtual size_t maxStatSuffixLength() const PURE;

  /**
   * The filter regex which limits stat creation.
   */
  virtual const absl::optional<std::regex>& statNameFilter() const PURE;
};

} // namespace Stats
} // namespace Envoy
