#include "source/common/stream_info/upstream_address.h"

namespace Envoy {
namespace StreamInfo {

absl::optional<std::string> UpstreamAddress::serializeAsString() const {
  auto address = getAddress();
  if (!address) {
    return {};
  }
  return address->asString();
}

} // namespace StreamInfo
} // namespace Envoy
