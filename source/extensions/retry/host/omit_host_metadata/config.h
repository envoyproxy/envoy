#pragma once

#include "envoy/extensions/retry/host/omit_host_metadata/v3/omit_host_metadata_config.pb.h"
#include "envoy/extensions/retry/host/omit_host_metadata/v3/omit_host_metadata_config.pb.validate.h"
#include "envoy/upstream/retry.h"

#include "extensions/retry/host/omit_host_metadata/omit_host_metadata.h"
#include "extensions/retry/host/well_known_names.h"

namespace Envoy {
namespace Extensions {
namespace Retry {
namespace Host {

class OmitHostsRetryPredicateFactory : public Upstream::RetryHostPredicateFactory {
public:
  Upstream::RetryHostPredicateSharedPtr createHostPredicate(const Protobuf::Message& config,
                                                            uint32_t retry_count) override;

  std::string name() const override {
    return RetryHostPredicateValues::get().OmitHostMetadataPredicate;
  }

  ProtobufTypes::MessagePtr createEmptyConfigProto() override {
    return ProtobufTypes::MessagePtr(
        new envoy::extensions::retry::host::omit_host_metadata::v3::OmitHostMetadataConfig());
  }
};

} // namespace Host
} // namespace Retry
} // namespace Extensions
} // namespace Envoy
