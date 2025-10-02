#include "source/extensions/http/ext_proc/processing_request_modifiers/mapped_attribute_builder/mapped_attribute_builder.h"

#include <string>

#include "envoy/extensions/http/ext_proc/processing_request_modifiers/mapped_attribute_builder/v3/mapped_attribute_builder.pb.h"
#include "envoy/stream_info/stream_info.h"

#include "source/extensions/filters/http/well_known_names.h"

namespace Envoy {
namespace Http {
namespace ExternalProcessing {

using envoy::service::ext_proc::v3::ProcessingRequest;

// Helper function to convert proto map values to a unique vector of strings.
// The order of elements in the returned vector is not guaranteed.
Protobuf::RepeatedPtrField<std::string>
protoMapValuesToUniqueVector(const Protobuf::Map<std::string, std::string>& proto_map) {
  absl::flat_hash_set<std::string> values;
  for (const auto& [_, value] : proto_map) {
    values.insert(value);
  }
  return {values.begin(), values.end()};
}

MappedAttributeBuilder::MappedAttributeBuilder(
    const envoy::extensions::http::ext_proc::processing_request_modifiers::
        mapped_attribute_builder::v3::MappedAttributeBuilder& config,
    Extensions::Filters::Common::Expr::BuilderInstanceSharedConstPtr expr_builder,
    Server::Configuration::CommonFactoryContext& context)
    : config_(config),
      expression_manager_(expr_builder, context.localInfo(),
                          protoMapValuesToUniqueVector(config.mapped_request_attributes()),
                          Protobuf::RepeatedPtrField<std::string>()) {}

bool MappedAttributeBuilder::modifyRequest(
    const Params& params, envoy::service::ext_proc::v3::ProcessingRequest& request) {
  if (params.traffic_direction != envoy::config::core::v3::TrafficDirection::INBOUND ||
      config_.mapped_request_attributes().empty() || sent_request_attributes_) {
    return false;
  }
  sent_request_attributes_ = true;

  auto activation_ptr = Extensions::Filters::Common::Expr::createActivation(
      &expression_manager_.localInfo(), params.callbacks->streamInfo(), params.request_headers,
      dynamic_cast<const Http::ResponseHeaderMap*>(params.response_headers),
      dynamic_cast<const Http::ResponseTrailerMap*>(params.response_trailers));

  const auto req_attributes = expression_manager_.evaluateRequestAttributes(*activation_ptr);

  Protobuf::Struct& remapped_attributes =
      (*request.mutable_attributes())[Extensions::HttpFilters::HttpFilterNames::get()
                                          .ExternalProcessing];
  remapped_attributes.clear_fields();
  for (const auto& pair : config_.mapped_request_attributes()) {
    const std::string& key = pair.first;
    const std::string& cel_expr_string = pair.second;
    auto it = req_attributes.fields().find(cel_expr_string);
    if (it != req_attributes.fields().end()) {
      (*remapped_attributes.mutable_fields())[key] = it->second;
    }
  }

  return true;
}

} // namespace ExternalProcessing
} // namespace Http
} // namespace Envoy
