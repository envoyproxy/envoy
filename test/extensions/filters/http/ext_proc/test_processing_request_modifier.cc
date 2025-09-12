#include "test/extensions/filters/http/ext_proc/test_processing_request_modifier.h"

#include <memory>
#include <string>

#include "source/extensions/filters/common/expr/evaluator.h"
#include "source/extensions/filters/http/ext_proc/matching_utils.h"
#include "source/extensions/filters/http/ext_proc/processing_request_modifier.h"

#include "test/extensions/filters/http/ext_proc/test_processing_request_modifier.pb.h"
#include "test/extensions/filters/http/ext_proc/test_processing_request_modifier.pb.validate.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace ExternalProcessing {

using envoy::service::ext_proc::v3::ProcessingRequest;
using envoy::test::extensions::filters::http::ext_proc::v3::TestProcessingRequestModifierConfig;

// Helper function to convert proto map values to a unique vector of strings.
// The order of elements in the returned vector is not guaranteed.
Protobuf::RepeatedPtrField<std::string>
ProtoMapValuesToUniqueVector(const Protobuf::Map<std::string, std::string>& proto_map) {
  absl::flat_hash_set<std::string> values;
  for (const auto& [_, value] : proto_map) {
    values.insert(value);
  }
  return {values.begin(), values.end()};
}

TestProcessingRequestModifier::TestProcessingRequestModifier(
    const TestProcessingRequestModifierConfig& config,
    Extensions::Filters::Common::Expr::BuilderInstanceSharedConstPtr builder,
    Server::Configuration::CommonFactoryContext& context)
    : config_(config),
      expression_manager_(builder, context.localInfo(),
                          ProtoMapValuesToUniqueVector(config.mapped_request_attributes()),
                          Protobuf::RepeatedPtrField<std::string>()) {}

bool TestProcessingRequestModifier::run(const Params& params,
                                        envoy::service::ext_proc::v3::ProcessingRequest& request) {
  if (params.traffic_direction != envoy::config::core::v3::TrafficDirection::INBOUND ||
      config_.mapped_request_attributes().empty() || sent_request_attributes_) {
    return false;
  }
  sent_request_attributes_ = true;

  auto activation_ptr = Filters::Common::Expr::createActivation(
      &expression_manager_.localInfo(), params.stream_info, params.request_headers,
      dynamic_cast<const Http::ResponseHeaderMap*>(params.response_headers),
      dynamic_cast<const Http::ResponseTrailerMap*>(params.response_trailers));

  auto req_attributes = expression_manager_.evaluateRequestAttributes(*activation_ptr);

  Protobuf::Struct& remapped_attributes =
      (*request.mutable_attributes())["envoy.filters.http.ext_proc"];
  for (const auto& pair : config_.mapped_request_attributes()) {
    const std::string& key = pair.first;
    const std::string& cel_expr_string = pair.second;
    if (req_attributes.fields().contains(cel_expr_string)) {
      (*remapped_attributes.mutable_fields())[key] = req_attributes.fields().at(cel_expr_string);
    }
  }

  return true;
}

std::unique_ptr<ProcessingRequestModifier>
TestProcessingRequestModifierFactory::createProcessingRequestModifier(
    const Protobuf::Message& config,
    Extensions::Filters::Common::Expr::BuilderInstanceSharedConstPtr builder,
    Envoy::Server::Configuration::CommonFactoryContext& context) const {
  const auto& proto_config =
      MessageUtil::downcastAndValidate<const TestProcessingRequestModifierConfig&>(
          config, context.messageValidationVisitor());
  return std::make_unique<TestProcessingRequestModifier>(proto_config, builder, context);
}

} // namespace ExternalProcessing
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
