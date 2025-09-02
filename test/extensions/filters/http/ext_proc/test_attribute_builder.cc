#include "test/extensions/filters/http/ext_proc/test_attribute_builder.h"

#include <memory>
#include <string>

#include "source/extensions/filters/common/expr/evaluator.h"
#include "source/extensions/filters/http/ext_proc/attribute_builder/attribute_builder.h"
#include "source/extensions/filters/http/ext_proc/matching_utils.h"

#include "test/extensions/filters/http/ext_proc/test_attribute_builder.pb.h"
#include "test/extensions/filters/http/ext_proc/test_attribute_builder.pb.validate.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace ExternalProcessing {

// Helper function to convert proto map values to a vector of strings.
// The order of elements in the returned vector is not guaranteed.
Protobuf::RepeatedPtrField<std::string>
ProtoMapValuesToVector(const Protobuf::Map<std::string, std::string>& proto_map) {
  Protobuf::RepeatedPtrField<std::string> values;
  for (const auto& [key, value] : proto_map) {
    *values.Add() = value;
  }
  return values;
}

TestAttributeBuilder::TestAttributeBuilder(
    const TestAttributeBuilderConfig& config,
    Extensions::Filters::Common::Expr::BuilderInstanceSharedConstPtr builder,
    Server::Configuration::CommonFactoryContext& context)
    : config_(config),
      expression_manager_(builder, context.localInfo(),
                          ProtoMapValuesToVector(config.mapped_request_attributes()),
                          Protobuf::RepeatedPtrField<std::string>()) {}

absl::optional<Protobuf::Struct> TestAttributeBuilder::build(const BuildParams& params) const {
  if (params.traffic_direction != envoy::config::core::v3::TrafficDirection::INBOUND ||
      config_.mapped_request_attributes().empty()) {
    return absl::nullopt;
  }

  auto activation_ptr = Filters::Common::Expr::createActivation(
      &expression_manager_.localInfo(), params.stream_info, params.request_headers,
      dynamic_cast<const Http::ResponseHeaderMap*>(params.response_headers),
      dynamic_cast<const Http::ResponseTrailerMap*>(params.response_trailers));

  auto attributes = expression_manager_.evaluateRequestAttributes(*activation_ptr);

  Protobuf::Struct remapped_attributes;
  for (const auto& pair : config_.mapped_request_attributes()) {
    const std::string& key = pair.first;
    const std::string& cel_expr_string = pair.second;
    if (attributes.fields().contains(cel_expr_string)) {
      (*remapped_attributes.mutable_fields())[key] = attributes.fields().at(cel_expr_string);
    }
  }
  return remapped_attributes;
}

std::unique_ptr<AttributeBuilder> TestAttributeBuilderFactory::createAttributeBuilder(
    const Protobuf::Message& config,
    Extensions::Filters::Common::Expr::BuilderInstanceSharedConstPtr builder,
    Envoy::Server::Configuration::CommonFactoryContext& context) const {
  const auto& proto_config = MessageUtil::downcastAndValidate<const TestAttributeBuilderConfig&>(
      config, context.messageValidationVisitor());
  return std::make_unique<TestAttributeBuilder>(proto_config, builder, context);
}

} // namespace ExternalProcessing
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
