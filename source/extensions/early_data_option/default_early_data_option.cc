#include "source/extensions/early_data_option/default_early_data_option.h"

#include "source/common/http/utility.h"

namespace Envoy {
namespace Router {

bool DefaultEarlyDataOption::allowsEarlyDataForRequest(
    const Http::RequestHeaderMap& request_headers) const {
  if (!allow_safe_request_) {
    return false;
  }
  return Http::Utility::isSafeRequest(request_headers);
}

REGISTER_FACTORY(DefaultEarlyDataOptionFactory, EarlyDataOptionFactory);

} // namespace Router
} // namespace Envoy
