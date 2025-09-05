#pragma once

#include "envoy/extensions/http/ext_proc/attribute_builders/default_attribute_builder/v3/default_attribute_builder.pb.h"
#include "envoy/extensions/http/ext_proc/attribute_builders/default_attribute_builder/v3/default_attribute_builder.pb.validate.h"
#include "envoy/server/factory_context.h"

#include "source/extensions/filters/common/expr/evaluator.h"
#include "source/extensions/filters/http/ext_proc/attribute_builder.h"
#include "source/extensions/filters/http/ext_proc/matching_utils.h"

#include "absl/types/optional.h"

namespace Envoy {
namespace Http {
namespace ExternalProcessing {

class DefaultAttributeBuilderFactory
    : public Extensions::HttpFilters::ExternalProcessing::AttributeBuilderFactory {
public:
  using DefaultAttributeBuilderProto = envoy::extensions::http::ext_proc::attribute_builders::
      default_attribute_builder::v3::DefaultAttributeBuilder;

  ~DefaultAttributeBuilderFactory() override = default;

  std::unique_ptr<Envoy::Extensions::HttpFilters::ExternalProcessing::AttributeBuilder>
  createAttributeBuilder(
      const Protobuf::Message& config, absl::string_view default_attribute_key,
      Extensions::Filters::Common::Expr::BuilderInstanceSharedConstPtr expr_builder,
      Server::Configuration::CommonFactoryContext& context) const override;

  ProtobufTypes::MessagePtr createEmptyConfigProto() override {
    return ProtobufTypes::MessagePtr{new DefaultAttributeBuilderProto()};
  }

  std::string name() const override;
};

} // namespace ExternalProcessing
} // namespace Http
} // namespace Envoy
