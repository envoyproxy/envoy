#pragma once

#include "envoy/extensions/retry/host/omit_host_metadata/v3/omit_host_metadata_config.pb.h"
#include "envoy/extensions/retry/host/omit_host_metadata/v3/omit_host_metadata_config.pb.validate.h"
#include "envoy/upstream/retry.h"

#include "source/extensions/retry/host/omit_host_metadata/omit_host_metadata.h"

namespace Envoy {
namespace Extensions {
namespace Retry {
namespace Host {

class OmitHostsRetryPredicateFactory : public Upstream::RetryHostPredicateFactory {
public:
  Upstream::RetryHostPredicateSharedPtr createHostPredicate(const Protobuf::Message& config,
                                                            uint32_t retry_count) override;

  std::string name() const override { return "envoy.retry_host_predicates.omit_host_metadata"; }

  ProtobufTypes::MessagePtr createEmptyConfigProto() override {
    return ProtobufTypes::MessagePtr(
        new envoy::extensions::retry::host::omit_host_metadata::v3::OmitHostMetadataConfig());
  }
};

} // namespace Host
} // namespace Retry
} // namespace Extensions
} // namespace Envoy
