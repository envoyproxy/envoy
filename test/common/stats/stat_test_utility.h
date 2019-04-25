#pragma once

#include "absl/strings/str_join.h"
#include "absl/strings/string_view.h"

namespace Envoy {
namespace Stats {
namespace TestUtil {

/**
 * Determines whether the library has deterministic malloc-stats results.
 *
 * @return bool true if the Memory::Stats::totalCurrentlyAllocated() has stable results.
 */
bool hasDeterministicMallocStats();

/**
 * Calls fn for a sampling of plausible stat names given a number of clusters.
 * This is intended for memory and performance benchmarking, where the syntax of
 * the names may be material to the measurements. Here we are deliberately not
 * claiming this is a complete stat set, which will change over time. Instead we
 * are aiming for consistency over time in order to create unit tests against
 * fixed memory budgets.
 *
 * @param num_clusters the number of clusters for which to generate stats.
 * @param fn the function to call with every stat name.
 */
void forEachSampleStat(int num_clusters, std::function<void(absl::string_view)> fn);

} // namespace TestUtil
} // namespace Stats
} // namespace Envoy
