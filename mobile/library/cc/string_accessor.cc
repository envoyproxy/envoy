#include "library/cc/string_accessor.h"

#include "library/common/data/utility.h"

namespace Envoy {
namespace Platform {

namespace {

envoy_data c_string_accessor_read(const void* context) {
  auto accessor = *static_cast<const StringAccessorSharedPtr*>(context);
  return Data::Utility::copyToBridgeData(accessor->get());
}

} // namespace

envoy_string_accessor StringAccessor::asEnvoyStringAccessor(StringAccessorSharedPtr accessor) {
  return envoy_string_accessor{&c_string_accessor_read, new StringAccessorSharedPtr(accessor)};
}

} // namespace Platform
} // namespace Envoy
