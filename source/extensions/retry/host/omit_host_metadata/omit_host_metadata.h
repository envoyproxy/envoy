#pragma once

#include "envoy/upstream/retry.h"
#include "envoy/upstream/upstream.h"

namespace Envoy {
namespace Extensions {
namespace Retry {
namespace Host {

class OmitHostsRetryPredicate : public Upstream::RetryHostPredicate {
public:
  OmitHostsRetryPredicate(envoy::config::core::v3alpha::Metadata metadata_match_criteria)
      : metadata_match_criteria_(metadata_match_criteria) {}

  bool shouldSelectAnotherHost(const Upstream::Host& host) override;

  void onHostAttempted(Upstream::HostDescriptionConstSharedPtr) override {}

private:
  const envoy::config::core::v3alpha::Metadata metadata_match_criteria_;
};

} // namespace Host
} // namespace Retry
} // namespace Extensions
} // namespace Envoy
