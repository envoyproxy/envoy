#pragma once

#include <algorithm>
#include <string>
#include <utility>
#include <vector>

#include "source/common/runtime/runtime_features.h"
#include "source/common/stats/thread_local_store.h"

#include "test/integration/server.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace Stats {
namespace TestUtil {

// Helpers for integration tests that must produce identical stats in both stats modes: the
// explicit-tags mode, where scopes and stats carry pre-computed tags, and the legacy mode, where
// tags are extracted from the flat stat names with the built-in regexes. Such tests exercise both
// modes without doubling the test matrix by running the two IP versions in different modes: v4 in
// the explicit-tags mode and v6 in the legacy mode.

/**
 * Sets the explicit-tags stats mode. The mode is read during server initialization, before the
 * runtime loader exists, so it has to be set directly rather than with a runtime override, and
 * before the test server is created.
 *
 * NOTE: this sets process-global state that is never reset, so the mode leaks into whatever tests
 * run after it in the same test binary. A fixture that creates a server without calling this runs
 * in the mode the previous test left behind, which is harmless only as long as it asserts nothing
 * that differs between the modes (flat stat names are identical in both).
 */
inline void setExplicitStatsTags(bool enable) {
  Runtime::maybeSetRuntimeGuard("envoy.reloadable_features.enable_stats_explicit_tags", enable);
}

/**
 * Verifies the server really runs in the requested stats mode, so that a parameterized test cannot
 * silently exercise the same mode twice.
 */
inline void verifyExplicitStatsTags(IntegrationTestServer& server, bool enable) {
  auto* store = dynamic_cast<Stats::ThreadLocalStoreImpl*>(&server.statStore());
  ASSERT_NE(store, nullptr);
  EXPECT_EQ(store->useExplicitTags(), enable);
}

/**
 * Checks a stat's flat name, the name it is tag-extracted to, and the tags attached to it, for a
 * counter, gauge or histogram. Both stats modes must produce identical results for every stat this
 * is used on.
 */
inline void expectStatTags(IntegrationTestServer& server, const std::string& name,
                           const std::string& tag_extracted_name,
                           const std::vector<std::pair<std::string, std::string>>& tags) {
  Stats::CounterSharedPtr counter = server.counter(name);
  Stats::GaugeSharedPtr gauge = server.gauge(name);
  Stats::ParentHistogramSharedPtr histogram = server.histogram(name);
  const Stats::Metric* metric = counter != nullptr ? static_cast<Stats::Metric*>(counter.get())
                                : gauge != nullptr ? static_cast<Stats::Metric*>(gauge.get())
                                                   : static_cast<Stats::Metric*>(histogram.get());
  ASSERT_NE(metric, nullptr) << "no counter, gauge or histogram named '" << name << "'";

  EXPECT_EQ(metric->tagExtractedName(), tag_extracted_name) << " for stat '" << name << "'";

  std::vector<std::pair<std::string, std::string>> actual_tags;
  for (const Stats::Tag& tag : metric->tags()) {
    actual_tags.emplace_back(tag.name_, tag.value_);
  }
  std::sort(actual_tags.begin(), actual_tags.end());
  std::vector<std::pair<std::string, std::string>> expected_tags = tags;
  std::sort(expected_tags.begin(), expected_tags.end());
  EXPECT_EQ(actual_tags, expected_tags) << " for stat '" << name << "'";
}

} // namespace TestUtil
} // namespace Stats
} // namespace Envoy
