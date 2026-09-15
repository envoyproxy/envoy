#include "source/extensions/filters/http/ai_protocol_manager/sse_event.h"

#include <memory>
#include <utility>

#include "source/common/buffer/buffer_impl.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace AiProtocolManager {

absl::string_view SseEvent::raw_data_as_string() {
  const uint64_t length = raw_data_->length();
  if (length == 0) {
    return {};
  }
  return absl::string_view(static_cast<const char*>(raw_data_->linearize(length)), length);
}

void SseEvent::set_raw_data(Buffer::InstancePtr raw_data) {
  raw_data_ = raw_data != nullptr ? std::move(raw_data) : std::make_unique<Buffer::OwnedImpl>();
  is_json_ = false;
  has_data_ = true;
  json_ = JsonWithExtBuf{};
  raw_data_ext_refs_.clear();
}

} // namespace AiProtocolManager
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
