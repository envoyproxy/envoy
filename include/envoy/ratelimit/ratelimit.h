#pragma once

#include <string>
#include <vector>

#include "envoy/config/typed_config.h"
#include "envoy/http/header_map.h"
#include "envoy/protobuf/message_validator.h"
#include "envoy/stream_info/stream_info.h"
#include "envoy/type/v3/ratelimit_unit.pb.h"

#include "absl/types/optional.h"

namespace Envoy {
namespace RateLimit {

/**
 * An optional dynamic override for the rate limit. See ratelimit.proto
 */
struct RateLimitOverride {
  uint32_t requests_per_unit_;
  envoy::type::v3::RateLimitUnit unit_;
};

/**
 * A single rate limit request descriptor entry. See ratelimit.proto.
 */
struct DescriptorEntry {
  std::string key_;
  std::string value_;
};

/**
 * A single rate limit request descriptor. See ratelimit.proto.
 */
struct Descriptor {
  std::vector<DescriptorEntry> entries_;
  absl::optional<RateLimitOverride> limit_ = absl::nullopt;
};

/**
 * Base interface for generic rate limit descriptor producer.
 */
class DescriptorProducer {
public:
  virtual ~DescriptorProducer() = default;

  /**
   * Potentially append a descriptor entry to the end of descriptor.
   * @param descriptor supplies the descriptor to optionally fill.
   * @param local_service_cluster supplies the name of the local service cluster.
   * @param headers supplies the header for the request.
   * @param info stream info associated with the request
   * @return true if the producer populated the descriptor.
   */
  virtual bool populateDescriptor(Descriptor& descriptor, const std::string& local_service_cluster,
                                  const Http::RequestHeaderMap& headers,
                                  const StreamInfo::StreamInfo& info) const PURE;
};

using DescriptorProducerPtr = std::unique_ptr<DescriptorProducer>;

/**
 * Implemented by each custom rate limit descriptor extension and registered via
 * Registry::registerFactory() or the convenience class RegisterFactory.
 */
class DescriptorProducerFactory : public Config::TypedFactory {
public:
  ~DescriptorProducerFactory() override = default;

  /**
   * Creates a particular DescriptorProducer implementation.
   *
   * @param config supplies the configuration for the descriptor extension.
   * @param validator configuration validation visitor.
   * @return DescriptorProducerPtr the rate limit descriptor producer which will be used to populate
   * rate limit descriptors.
   */
  virtual DescriptorProducerPtr
  createDescriptorProducerFromProto(const Protobuf::Message& config,
                                    ProtobufMessage::ValidationVisitor& validator) PURE;

  std::string category() const override { return "envoy.rate_limit_descriptors"; }
};

} // namespace RateLimit
} // namespace Envoy
