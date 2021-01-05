#pragma once

#include "envoy/ratelimit/ratelimit.h"
#include "envoy/registry/registry.h"

#include "extensions/filters/common/expr/evaluator.h"

namespace Envoy {
namespace Extensions {
namespace Descriptors {
namespace Expr {

/**
 * Config registration for the computed rate limit descriptor.
 * @see DescriptorProducerFactory.
 */
class ExprDescriptorFactory : public RateLimit::DescriptorProducerFactory {
public:
  std::string name() const override { return "envoy.descriptors.expr"; }
  ProtobufTypes::MessagePtr createEmptyConfigProto() override;
  RateLimit::DescriptorProducerPtr
  createDescriptorProducerFromProto(const Protobuf::Message& message,
                                    ProtobufMessage::ValidationVisitor& validator) override;

private:
  Filters::Common::Expr::Builder& getOrCreateBuilder();
  Filters::Common::Expr::BuilderPtr expr_builder_;
};

DECLARE_FACTORY(ExprDescriptorFactory);

} // namespace Expr
} // namespace Descriptors
} // namespace Extensions
} // namespace Envoy
