#pragma once

#include "envoy/ratelimit/ratelimit.h"
#include "envoy/registry/registry.h"

namespace Envoy {
namespace Extensions {
namespace RateLimitDescriptors {
namespace JwtClaim {

/**
 * Config registration for the JWT claim rate limit descriptor.
 * @see DescriptorProducerFactory.
 */
class JwtClaimDescriptorFactory : public RateLimit::DescriptorProducerFactory {
public:
  std::string name() const override;
  ProtobufTypes::MessagePtr createEmptyConfigProto() override;
  absl::StatusOr<RateLimit::DescriptorProducerPtr>
  createDescriptorProducerFromProto(const Protobuf::Message& message,
                                    Server::Configuration::CommonFactoryContext& context) override;
};

} // namespace JwtClaim
} // namespace RateLimitDescriptors
} // namespace Extensions
} // namespace Envoy
