#include "source/extensions/http/ext_proc/processing_request_modifiers/mapped_attribute_builder/mapped_attribute_builder_factory.h"

#include <memory>
#include <string>

#include "source/extensions/filters/common/expr/evaluator.h"
#include "source/extensions/filters/http/ext_proc/matching_utils.h"
#include "source/extensions/filters/http/ext_proc/processing_request_modifier.h"

namespace Envoy {
namespace Http {
namespace ExternalProcessing {

std::unique_ptr<Envoy::Extensions::HttpFilters::ExternalProcessing::ProcessingRequestModifier>
MappedAttributeBuilderFactory::createProcessingRequestModifier(
    const Protobuf::Message& config,
    Extensions::Filters::Common::Expr::BuilderInstanceSharedConstPtr builder,
    Server::Configuration::CommonFactoryContext& context) const {
  const auto& proto_config = MessageUtil::downcastAndValidate<
      const envoy::extensions::http::ext_proc::processing_request_modifiers::
          mapped_attribute_builder::v3::MappedAttributeBuilder&>(
      config, context.messageValidationVisitor());
  return std::make_unique<MappedAttributeBuilder>(proto_config, builder, context);
}

REGISTER_FACTORY(
    MappedAttributeBuilderFactory,
    Envoy::Extensions::HttpFilters::ExternalProcessing::ProcessingRequestModifierFactory);

} // namespace ExternalProcessing
} // namespace Http
} // namespace Envoy
