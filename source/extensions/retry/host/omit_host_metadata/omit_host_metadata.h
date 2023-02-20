#pragma once

#include "envoy/upstream/retry.h"
#include "envoy/upstream/upstream.h"

#include "source/common/config/well_known_names.h"

namespace Envoy {
namespace Extensions {
namespace Retry {
namespace Host {

class OmitHostsRetryPredicate : public Upstream::RetryHostPredicate {
public:
  explicit OmitHostsRetryPredicate(const envoy::config::core::v3::Metadata& metadata_match_criteria)
      : metadata_match_criteria_(metadata_match_criteria) {
    const auto& filter_it = metadata_match_criteria_.filter_metadata().find(
        Envoy::Config::MetadataFilters::get().ENVOY_LB);
    if (filter_it != metadata_match_criteria_.filter_metadata().end()) {
      for (auto const& it : filter_it->second.fields()) {
        label_set_.push_back(it);
      }
    }
  }

  bool shouldSelectAnotherHost(const Upstream::Host& host) override;

  void onHostAttempted(Upstream::HostDescriptionConstSharedPtr) override {}

private:
  const envoy::config::core::v3::Metadata metadata_match_criteria_;
  std::vector<std::pair<std::string, ProtobufWkt::Value>> label_set_;
};

} // namespace Host
} // namespace Retry
} // namespace Extensions
} // namespace Envoy
