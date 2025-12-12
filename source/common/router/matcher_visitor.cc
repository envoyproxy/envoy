#include "source/common/router/matcher_visitor.h"

#include "envoy/extensions/matching/common_inputs/network/v3/network_inputs.pb.h"
#include "envoy/extensions/matching/common_inputs/network/v3/network_inputs.pb.validate.h"
#include "envoy/type/matcher/v3/http_inputs.pb.h"
#include "envoy/type/matcher/v3/http_inputs.pb.validate.h"

namespace Envoy {
namespace Router {

absl::Status RouteActionValidationVisitor::performDataInputValidation(
    const Matcher::DataInputFactory<Http::HttpMatchingData>&, absl::string_view type_url) {
  static const std::string request_header_input_name = TypeUtil::descriptorFullNameToTypeUrl(
      createReflectableMessage(
          envoy::type::matcher::v3::HttpRequestHeaderMatchInput::default_instance())
          ->GetDescriptor()
          ->full_name());
  static const std::string filter_state_input_name = TypeUtil::descriptorFullNameToTypeUrl(
      createReflectableMessage(envoy::extensions::matching::common_inputs::network::v3::
                                   FilterStateInput::default_instance())
          ->GetDescriptor()
          ->full_name());
  if (type_url == request_header_input_name || type_url == filter_state_input_name) {
    return absl::OkStatus();
  }

  return absl::InvalidArgumentError(
      fmt::format("Route table can only match on request headers, saw {}", type_url));
}

} // namespace Router
} // namespace Envoy
