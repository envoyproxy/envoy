#include "source/common/listener_manager/listener_info_impl.h"

namespace Envoy {
namespace Server {

const envoy::config::core::v3::Metadata& ListenerInfoImpl::metadata() const {
  return config_.metadata();
}
const Envoy::Config::TypedMetadata& ListenerInfoImpl::typedMetadata() const {
  return typed_metadata_;
}

} // namespace Server
} // namespace Envoy
