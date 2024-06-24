#pragma once

#include "source/common/common/utility.h"

#include "contrib/golang/common/dso/dso.h"

namespace Envoy {
namespace Extensions {
namespace Common {
namespace Golang {

class FilterLogger : Logger::Loggable<Logger::Id::golang> {
public:
  FilterLogger() = default;

  void log(uint32_t level, absl::string_view message) const;
  uint32_t level() const;
};

} // namespace Golang
} // namespace Common
} // namespace Extensions
} // namespace Envoy
