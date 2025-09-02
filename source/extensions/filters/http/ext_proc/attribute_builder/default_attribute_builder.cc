#include "source/extensions/filters/http/ext_proc/attribute_builder/default_attribute_builder.h"

#include "envoy/http/header_map.h"

#include "source/extensions/filters/common/expr/evaluator.h"
#include "source/extensions/filters/http/ext_proc/matching_utils.h"

#include "absl/types/optional.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace ExternalProcessing {

DefaultAttributeBuilder::DefaultAttributeBuilder(
    const Protobuf::RepeatedPtrField<std::string>& request_attributes,
    const Protobuf::RepeatedPtrField<std::string>& response_attributes,
    absl::string_view default_attribute_key,
    Extensions::Filters::Common::Expr::BuilderInstanceSharedConstPtr builder,
    Server::Configuration::CommonFactoryContext& context)
    : default_attribute_key_(default_attribute_key),
      expression_manager_(builder, context.localInfo(), request_attributes, response_attributes) {}

bool DefaultAttributeBuilder::build(
    const BuildParams& params, Protobuf::Map<std::string, Protobuf::Struct>* attributes) const {
  bool should_send = false;
  if (params.traffic_direction == envoy::config::core::v3::TrafficDirection::INBOUND) {
    should_send = expression_manager_.hasRequestExpr();
  } else {
    should_send = expression_manager_.hasResponseExpr();
  }

  if (!should_send) {
    return false;
  }

  auto activation_ptr = Filters::Common::Expr::createActivation(
      &expression_manager_.localInfo(), params.stream_info, params.request_headers,
      dynamic_cast<const Http::ResponseHeaderMap*>(params.response_headers),
      dynamic_cast<const Http::ResponseTrailerMap*>(params.response_trailers));
  if (params.traffic_direction == envoy::config::core::v3::TrafficDirection::INBOUND) {
    (*attributes)[default_attribute_key_] =
        expression_manager_.evaluateRequestAttributes(*activation_ptr);
  } else {
    (*attributes)[default_attribute_key_] =
        expression_manager_.evaluateResponseAttributes(*activation_ptr);
  }
  return true;
}

} // namespace ExternalProcessing
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
