#pragma once

#include "envoy/extensions/http/ext_proc/processing_request_modifiers/mapped_attribute_builder/v3/mapped_attribute_builder.pb.h"
#include "envoy/extensions/http/ext_proc/processing_request_modifiers/mapped_attribute_builder/v3/mapped_attribute_builder.pb.validate.h"
#include "envoy/server/factory_context.h"

#include "source/extensions/filters/common/expr/evaluator.h"
#include "source/extensions/filters/http/ext_proc/matching_utils.h"
#include "source/extensions/filters/http/ext_proc/processing_request_modifier.h"
#include "source/extensions/http/ext_proc/processing_request_modifiers/mapped_attribute_builder/mapped_attribute_builder.h"

namespace Envoy {
namespace Http {
namespace ExternalProcessing {

class MappedAttributeBuilderFactory
    : public Envoy::Extensions::HttpFilters::ExternalProcessing::ProcessingRequestModifierFactory {
public:
  ~MappedAttributeBuilderFactory() override = default;
  std::unique_ptr<Envoy::Extensions::HttpFilters::ExternalProcessing::ProcessingRequestModifier>
  createProcessingRequestModifier(
      const Protobuf::Message& config,
      Extensions::Filters::Common::Expr::BuilderInstanceSharedConstPtr builder,
      Envoy::Server::Configuration::CommonFactoryContext& context) const override;

  ProtobufTypes::MessagePtr createEmptyConfigProto() override {
    return ProtobufTypes::MessagePtr{
        new envoy::extensions::http::ext_proc::processing_request_modifiers::
            mapped_attribute_builder::v3::MappedAttributeBuilder()};
  }

  std::string name() const override {
    return "envoy.extensions.http.ext_proc.mapped_attribute_builder";
  }
};

} // namespace ExternalProcessing
} // namespace Http
} // namespace Envoy
