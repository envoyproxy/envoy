#pragma once

#include "absl/strings/str_join.h"
#include "absl/strings/string_view.h"

namespace Envoy {
namespace Stats {
namespace TestUtil {

void foreachStat(int num_clusters, std::function<void(absl::string_view)> fn);

} // namespace TestUtil
} // namespace Stats
} // namespace Envoy
