#include "source/extensions/early_data/default_early_data_policy.h"

#include "source/common/http/utility.h"

namespace Envoy {
namespace Router {

bool DefaultEarlyDataPolicy::allowsEarlyDataForRequest(
    const Http::RequestHeaderMap& request_headers) const {
  if (!allow_safe_request_) {
    return false;
  }
  return Http::Utility::isSafeRequest(request_headers);
}

REGISTER_FACTORY(DefaultEarlyDataPolicyFactory, EarlyDataPolicyFactory);

} // namespace Router
} // namespace Envoy
