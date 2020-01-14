#pragma once

#include "envoy/upstream/retry.h"
#include "envoy/upstream/upstream.h"

#include "common/config/well_known_names.h"

namespace Envoy {
namespace Extensions {
namespace Retry {
namespace Host {

class OmitHostsRetryPredicate : public Upstream::RetryHostPredicate {
public:
  explicit OmitHostsRetryPredicate(
      const envoy::config::core::v3alpha::Metadata metadata_match_criteria)
      : metadata_match_criteria_(metadata_match_criteria) {
    const auto& filter_it = metadata_match_criteria_.filter_metadata().find(
        Envoy::Config::MetadataFilters::get().ENVOY_LB);
    if (filter_it == metadata_match_criteria_.filter_metadata().end()) {
      throw EnvoyException("No metadata match criteria defined.");
    }

    for (auto const& it : filter_it->second.fields()) {
      labelSet.push_back(std::make_pair(it.first, it.second));
    }
  }

  bool shouldSelectAnotherHost(const Upstream::Host& host) override;

  void onHostAttempted(Upstream::HostDescriptionConstSharedPtr) override {}

private:
  const envoy::config::core::v3alpha::Metadata metadata_match_criteria_;
  std::vector<std::pair<std::string, ProtobufWkt::Value>> labelSet;
};

} // namespace Host
} // namespace Retry
} // namespace Extensions
} // namespace Envoy
