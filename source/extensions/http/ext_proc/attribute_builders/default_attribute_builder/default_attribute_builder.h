#pragma once

#include "envoy/extensions/http/ext_proc/attribute_builders/default_attribute_builder/v3/default_attribute_builder.pb.h"
#include "envoy/extensions/http/ext_proc/attribute_builders/default_attribute_builder/v3/default_attribute_builder.pb.validate.h"

#include "source/extensions/filters/common/expr/evaluator.h"
#include "source/extensions/filters/http/ext_proc/attribute_builder.h"
#include "source/extensions/filters/http/ext_proc/matching_utils.h"

#include "absl/types/optional.h"

namespace Envoy {
namespace Http {
namespace ExternalProcessing {

class DefaultAttributeBuilder
    : public Extensions::HttpFilters::ExternalProcessing::AttributeBuilder::AttributeBuilder {
public:
  using DefaultAttributeBuilderProto = envoy::extensions::http::ext_proc::attribute_builders::
      default_attribute_builder::v3::DefaultAttributeBuilder;

  DefaultAttributeBuilder(const DefaultAttributeBuilderProto& config,
                          absl::string_view default_attribute_key,
                          Extensions::Filters::Common::Expr::BuilderInstanceSharedConstPtr builder,
                          Server::Configuration::CommonFactoryContext& context);

  bool build(const BuildParams& params,
             Protobuf::Map<std::string, Protobuf::Struct>* attributes) const override;

private:
  const std::string default_attribute_key_;
  const Extensions::HttpFilters::ExternalProcessing::ExpressionManager expression_manager_;
};

} // namespace ExternalProcessing
} // namespace Http
} // namespace Envoy
