#include "source/server/options_impl_base.h"

#include <chrono>
#include <cstdint>
#include <iostream>
#include <string>

#include "envoy/admin/v3/server_info.pb.h"

#include "source/common/common/fmt.h"
#include "source/common/common/logger.h"
#include "source/common/common/macros.h"
#include "source/common/protobuf/utility.h"
#include "source/common/stats/tag_utility.h"
#include "source/common/version/version.h"

#include "absl/strings/str_replace.h"
#include "absl/strings/str_split.h"
#include "absl/strings/string_view.h"
#include "spdlog/spdlog.h"

namespace Envoy {

absl::StatusOr<spdlog::level::level_enum>
OptionsImplBase::parseAndValidateLogLevel(absl::string_view log_level) {
  if (log_level == "warn") {
    return spdlog::level::level_enum::warn;
  }

  size_t level_to_use = std::numeric_limits<size_t>::max();
  for (size_t i = 0; i < ARRAY_SIZE(spdlog::level::level_string_views); i++) {
    spdlog::string_view_t spd_log_level = spdlog::level::level_string_views[i];
    if (log_level == absl::string_view(spd_log_level.data(), spd_log_level.size())) {
      level_to_use = i;
      break;
    }
  }

  if (level_to_use == std::numeric_limits<size_t>::max()) {
    return absl::InvalidArgumentError(
        fmt::format("error: invalid log level specified '{}'", log_level));
  }
  return static_cast<spdlog::level::level_enum>(level_to_use);
}

absl::Status OptionsImplBase::setLogLevel(absl::string_view log_level) {
  auto status_or_level = parseAndValidateLogLevel(log_level);
  if (!status_or_level.status().ok()) {
    return status_or_level.status();
  }
  setLogLevel(status_or_level.value());
  return absl::OkStatus();
}

uint32_t OptionsImplBase::count() const { return count_; }

void OptionsImplBase::disableExtensions(const std::vector<std::string>& names) {
  for (const auto& name : names) {
    const std::vector<absl::string_view> parts = absl::StrSplit(name, absl::MaxSplits('/', 1));

    if (parts.size() != 2) {
      ENVOY_LOG_MISC(warn, "failed to disable invalid extension name '{}'", name);
      continue;
    }

    if (Registry::FactoryCategoryRegistry::disableFactory(parts[0], parts[1])) {
      ENVOY_LOG_MISC(info, "disabled extension '{}'", name);
    } else {
      ENVOY_LOG_MISC(warn, "failed to disable unknown extension '{}'", name);
    }
  }
}

} // namespace Envoy
