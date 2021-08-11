#pragma once

#include "envoy/ratelimit/ratelimit.h"
#include "envoy/registry/registry.h"

#include "source/extensions/filters/common/expr/evaluator.h"

namespace Envoy {
namespace Extensions {
namespace RateLimitDescriptors {
namespace Expr {

/**
 * Config registration for the computed rate limit descriptor.
 * @see DescriptorProducerFactory.
 */
class ExprDescriptorFactory : public RateLimit::DescriptorProducerFactory {
public:
  std::string name() const override;
  ProtobufTypes::MessagePtr createEmptyConfigProto() override;
  RateLimit::DescriptorProducerPtr
  createDescriptorProducerFromProto(const Protobuf::Message& message,
                                    ProtobufMessage::ValidationVisitor& validator) override;

private:
  Filters::Common::Expr::Builder& getOrCreateBuilder();
  Filters::Common::Expr::BuilderPtr expr_builder_;
};

} // namespace Expr
} // namespace RateLimitDescriptors
} // namespace Extensions
} // namespace Envoy
