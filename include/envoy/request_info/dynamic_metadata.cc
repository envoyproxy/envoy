#include "envoy/request_info/dynamic_metadata.h"

namespace Envoy {
namespace RequestInfo {

std::atomic<size_t> DynamicMetadata::type_id_index_(0u);

} // namespace RequestInfo
} // namespace Envoy
