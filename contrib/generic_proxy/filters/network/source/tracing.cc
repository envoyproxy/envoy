#include "contrib/generic_proxy/filters/network/source/tracing.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace GenericProxy {

absl::string_view TraceContextBridge::protocol() const { return request_.protocol(); }
absl::string_view TraceContextBridge::host() const { return request_.host(); }
absl::string_view TraceContextBridge::path() const { return request_.path(); }
absl::string_view TraceContextBridge::method() const { return request_.method(); }
void TraceContextBridge::forEach(IterateCallback callback) const { request_.forEach(callback); }
absl::optional<absl::string_view> TraceContextBridge::getByKey(absl::string_view key) const {
  return request_.get(key);
}
void TraceContextBridge::setByKey(absl::string_view key, absl::string_view val) {
  request_.set(key, val);
}
void TraceContextBridge::setByReferenceKey(absl::string_view key, absl::string_view val) {
  request_.set(key, val);
}
void TraceContextBridge::setByReference(absl::string_view key, absl::string_view val) {
  request_.set(key, val);
}
void TraceContextBridge::removeByKey(absl::string_view key) { request_.erase(key); }

} // namespace GenericProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
