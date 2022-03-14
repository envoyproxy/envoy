#include "source/extensions/filters/http/gcp_authn/gcp_authn_filter.h"

#include "source/common/common/enum_to_int.h"
#include "source/common/http/header_map_impl.h"
#include "source/common/http/utility.h"

#include "jwt_verify_lib/jwt.h"
#include "jwt_verify_lib/verify.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace GcpAuthn {

using ::google::jwt_verify::Status;
using Http::FilterHeadersStatus;

Http::FilterHeadersStatus GcpAuthnFilter::decodeHeaders(Http::RequestHeaderMap&, bool) {
  state_ = State::Calling;
  client_->fetchToken(*this);
  return Envoy::Http::FilterHeadersStatus::Continue;
  return state_ == State::Complete ? FilterHeadersStatus::Continue
                                   : Http::FilterHeadersStatus::StopIteration;
}

// TODO(tyxia) the lifetime of response should be fine?
void GcpAuthnFilter::onComplete(const Http::ResponseMessage* response) {
  state_ = State::Complete;
  // Process the response if it is present (i.e., received the response successfully)
  if (response != nullptr) {
    // Decode JWT Token
    ::google::jwt_verify::Jwt jwt;
    Status status = jwt.parseFromString(response->bodyAsString());
    if (status == Status::Ok) {
    }
  }
}

void GcpAuthnFilter::onDestroy() {
  if (state_ == State::Calling) {
    state_ = State::Complete;
    client_->cancel();
  }
}

} // namespace GcpAuthn
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
