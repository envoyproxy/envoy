#pragma once

#include <string>
#include <vector>

#include "envoy/config/typed_config.h"
#include "envoy/http/header_map.h"
#include "envoy/protobuf/message_validator.h"
#include "envoy/server/factory_context.h"
#include "envoy/stream_info/stream_info.h"
#include "envoy/type/v3/ratelimit_unit.pb.h"

#include "absl/time/time.h"
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

  friend bool operator==(const DescriptorEntry& lhs, const DescriptorEntry& rhs) {
    return lhs.key_ == rhs.key_ && lhs.value_ == rhs.value_;
  }
  template <typename H>
  friend H AbslHashValue(H h, // NOLINT(readability-identifier-naming)
                         const DescriptorEntry& entry) {
    return H::combine(std::move(h), entry.key_, entry.value_);
  }
};

/**
 * A single rate limit request descriptor. See ratelimit.proto.
 */
struct Descriptor {
  std::vector<DescriptorEntry> entries_;
  absl::optional<RateLimitOverride> limit_ = absl::nullopt;
};

/**
 * A single token bucket. See token_bucket.proto.
 */
struct TokenBucket {
  uint32_t max_tokens_;
  uint32_t tokens_per_fill_;
  absl::Duration fill_interval_;
};

/**
 * A single rate limit request descriptor. See ratelimit.proto.
 */
struct LocalDescriptor {
  std::vector<DescriptorEntry> entries_;
  friend bool operator==(const LocalDescriptor& lhs, const LocalDescriptor& rhs) {
    return lhs.entries_ == rhs.entries_;
  }
};

/*
 * Base interface for generic rate limit descriptor producer.
 */
class DescriptorProducer {
public:
  virtual ~DescriptorProducer() = default;

  /**
   * Potentially fill a descriptor entry to the end of descriptor.
   * @param descriptor_entry supplies the descriptor entry to optionally fill.
   * @param local_service_cluster supplies the name of the local service cluster.
   * @param headers supplies the header for the request.
   * @param info stream info associated with the request
   * @return true if the producer populated the descriptor.
   */
  virtual bool populateDescriptor(DescriptorEntry& descriptor_entry,
                                  const std::string& local_service_cluster,
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
   * @param context supplies the factory context.
   * @return DescriptorProducerPtr the rate limit descriptor producer which will be used to
   * populate rate limit descriptors.
   */
  virtual DescriptorProducerPtr
  createDescriptorProducerFromProto(const Protobuf::Message& config,
                                    Server::Configuration::CommonFactoryContext& context) PURE;

  std::string category() const override { return "envoy.rate_limit_descriptors"; }
};

} // namespace RateLimit
} // namespace Envoy
