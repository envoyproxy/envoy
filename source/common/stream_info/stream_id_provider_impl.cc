#include "source/common/stream_info/stream_id_provider_impl.h"

#include "source/common/common/utility.h"

namespace Envoy {
namespace StreamInfo {

absl::optional<uint64_t> StreamIdProviderImpl::toInteger() const {
  if (id_.length() < 8) {
    return absl::nullopt;
  }

  uint64_t value;
  if (!StringUtil::atoull(id_.substr(0, 8).c_str(), value, 16)) {
    return absl::nullopt;
  }

  return value;
}

} // namespace StreamInfo
} // namespace Envoy
