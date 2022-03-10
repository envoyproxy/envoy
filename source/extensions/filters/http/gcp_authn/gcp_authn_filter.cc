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
  return state_ == State::Complete ? FilterHeadersStatus::Continue
                                   : Http::FilterHeadersStatus::StopIteration;
}

// TODO(tyxia) the lifetime of response should be fine?
void GcpAuthnFilter::onComplete(ResponseStatus, const Http::ResponseMessage* response) {
  // TODO(tyxia) If failed, still complete?
  // Do I still need ResponseStatus?
  state_ = State::Complete;
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
    // TODO(tyxia) Why this is inside
    client_->cancel();
  }
}

} // namespace GcpAuthn
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
