#pragma once

#include "envoy/server/factory_context.h"

#include "source/extensions/filters/common/expr/evaluator.h"
#include "source/extensions/filters/http/ext_proc/attribute_builder/attribute_builder.h"
#include "source/extensions/filters/http/ext_proc/matching_utils.h"

#include "absl/types/optional.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace ExternalProcessing {

class DefaultAttributeBuilder : public AttributeBuilder {
public:
  DefaultAttributeBuilder(const Protobuf::RepeatedPtrField<std::string>& request_attributes,
                          const Protobuf::RepeatedPtrField<std::string>& response_attributes,
                          absl::string_view default_attribute_key,
                          Extensions::Filters::Common::Expr::BuilderInstanceSharedConstPtr builder,
                          Server::Configuration::CommonFactoryContext& context);

  bool build(const BuildParams& params,
             Protobuf::Map<std::string, Protobuf::Struct>* attributes) const override;

private:
  const std::string default_attribute_key_;
  const ExpressionManager expression_manager_;
};

} // namespace ExternalProcessing
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
