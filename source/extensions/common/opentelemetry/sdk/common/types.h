#pragma once

#include <cstdint>
#include <map>
#include <string>
#include <vector>

#include "absl/strings/string_view.h"
#include "absl/types/variant.h"

namespace Envoy {
namespace Extensions {
namespace OpenTelemetry {
namespace Sdk {
namespace Common {

using AttributeValue =
    absl::variant<bool, int32_t, uint32_t, int64_t, double, std::string, absl::string_view,
                  std::vector<bool>, std::vector<int32_t>, std::vector<uint32_t>,
                  std::vector<int64_t>, std::vector<double>, std::vector<std::string>,
                  std::vector<absl::string_view>, uint64_t, std::vector<uint64_t>,
                  std::vector<uint8_t>>;

using OwnedAttributeMap = std::map<std::string, AttributeValue>;

} // namespace Common
} // namespace Sdk
} // namespace OpenTelemetry
} // namespace Extensions
} // namespace Envoy
