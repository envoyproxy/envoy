#include "source/common/listener_manager/listener_info_impl.h"

namespace Envoy {
namespace Server {

const envoy::config::core::v3::Metadata& ListenerInfoImpl::metadata() const {
  return metadata_.proto_metadata_;
}
const Envoy::Config::TypedMetadata& ListenerInfoImpl::typedMetadata() const {
  return metadata_.typed_metadata_;
}

} // namespace Server
} // namespace Envoy
