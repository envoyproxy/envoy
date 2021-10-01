#include "contrib/sip_proxy/filters/network/source/app_exception_impl.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace SipProxy {

DirectResponse::ResponseType AppException::encode(MessageMetadata& metadata,
                                                  Buffer::Instance& buffer) const {
  (void)metadata;
  (void)buffer;

  // TODO

  return DirectResponse::ResponseType::Exception;
}

} // namespace SipProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
