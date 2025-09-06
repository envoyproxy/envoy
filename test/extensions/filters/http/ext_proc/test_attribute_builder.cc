#include "test/extensions/filters/http/ext_proc/test_attribute_builder.h"

#include <memory>
#include <string>

#include "source/extensions/filters/common/expr/evaluator.h"
#include "source/extensions/filters/http/ext_proc/attribute_builder.h"
#include "source/extensions/filters/http/ext_proc/matching_utils.h"

#include "test/extensions/filters/http/ext_proc/test_attribute_builder.pb.h"
#include "test/extensions/filters/http/ext_proc/test_attribute_builder.pb.validate.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace ExternalProcessing {

using envoy::test::extensions::filters::http::ext_proc::v3::TestAttributeBuilderConfig;

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

TestAttributeBuilder::TestAttributeBuilder(
    const TestAttributeBuilderConfig& config, absl::string_view default_attribute_key,
    Extensions::Filters::Common::Expr::BuilderInstanceSharedConstPtr builder,
    Server::Configuration::CommonFactoryContext& context)
    : config_(config), default_attribute_key_(default_attribute_key),
      expression_manager_(builder, context.localInfo(),
                          ProtoMapValuesToUniqueVector(config.mapped_request_attributes()),
                          Protobuf::RepeatedPtrField<std::string>()) {}

bool TestAttributeBuilder::build(const BuildParams& params,
                                 Protobuf::Map<std::string, Protobuf::Struct>* attributes) const {
  if (params.traffic_direction != envoy::config::core::v3::TrafficDirection::INBOUND ||
      config_.mapped_request_attributes().empty()) {
    return false;
  }

  auto activation_ptr = Filters::Common::Expr::createActivation(
      &expression_manager_.localInfo(), params.stream_info, params.request_headers,
      dynamic_cast<const Http::ResponseHeaderMap*>(params.response_headers),
      dynamic_cast<const Http::ResponseTrailerMap*>(params.response_trailers));

  auto req_attributes = expression_manager_.evaluateRequestAttributes(*activation_ptr);

  Protobuf::Struct& remapped_attributes = (*attributes)[default_attribute_key_];
  for (const auto& pair : config_.mapped_request_attributes()) {
    const std::string& key = pair.first;
    const std::string& cel_expr_string = pair.second;
    if (req_attributes.fields().contains(cel_expr_string)) {
      (*remapped_attributes.mutable_fields())[key] = req_attributes.fields().at(cel_expr_string);
    }
  }
  return true;
}

std::unique_ptr<AttributeBuilder> TestAttributeBuilderFactory::createAttributeBuilder(
    const Protobuf::Message& config, absl::string_view default_attribute_key,
    Extensions::Filters::Common::Expr::BuilderInstanceSharedConstPtr builder,
    Envoy::Server::Configuration::CommonFactoryContext& context) const {
  const auto& proto_config = MessageUtil::downcastAndValidate<const TestAttributeBuilderConfig&>(
      config, context.messageValidationVisitor());
  return std::make_unique<TestAttributeBuilder>(proto_config, default_attribute_key, builder,
                                                context);
}

} // namespace ExternalProcessing
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
