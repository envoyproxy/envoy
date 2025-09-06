#include "source/extensions/http/ext_proc/attribute_builders/default_attribute_builder/default_attribute_builder_factory.h"

#include <string>

#include "envoy/registry/registry.h"

#include "source/extensions/http/ext_proc/attribute_builders/default_attribute_builder/default_attribute_builder.h"

namespace Envoy {
namespace Http {
namespace ExternalProcessing {

std::string DefaultAttributeBuilderFactory::name() const {
  return "envoy.extensions.http.ext_proc.default_attribute_builder";
}

std::unique_ptr<Envoy::Extensions::HttpFilters::ExternalProcessing::AttributeBuilder>
DefaultAttributeBuilderFactory::createAttributeBuilder(
    const Protobuf::Message& config, absl::string_view default_attribute_key,
    Extensions::Filters::Common::Expr::BuilderInstanceSharedConstPtr expr_builder,
    Server::Configuration::CommonFactoryContext& context) const {
  const DefaultAttributeBuilderProto& default_attribute_builder_config =
      MessageUtil::downcastAndValidate<const DefaultAttributeBuilderProto&>(
          config, context.messageValidationVisitor());
  return std::make_unique<DefaultAttributeBuilder>(default_attribute_builder_config,
                                                   default_attribute_key, expr_builder, context);
}

REGISTER_FACTORY(DefaultAttributeBuilderFactory,
                 Envoy::Extensions::HttpFilters::ExternalProcessing::AttributeBuilderFactory);

} // namespace ExternalProcessing
} // namespace Http
} // namespace Envoy
