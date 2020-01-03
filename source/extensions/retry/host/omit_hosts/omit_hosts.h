#pragma once

#include "envoy/upstream/retry.h"
#include "envoy/upstream/upstream.h"

#include "extensions/retry/host/well_known_names.h"

namespace Envoy {
class OmitHostsRetryPredicate : public Upstream::RetryHostPredicate {
public:
  OmitHostsRetryPredicate(envoy::api::v2::core::Metadata metadata_match_criteria,
                          uint32_t retry_count)
      : attempted_hosts_(retry_count), metadata_match_criteria_(metadata_match_criteria) {}

  bool shouldSelectAnotherHost(const Upstream::Host& host) override {
    const auto& match_criteria_filter_it = metadata_match_criteria_.filter_metadata().find(
        Extensions::Retry::Host::MetadataFilters::get().ENVOY_LB);
    const auto& host_metadata_filter_it = host.metadata()->filter_metadata().find(
        Extensions::Retry::Host::MetadataFilters::get().ENVOY_LB);

    if (match_criteria_filter_it == metadata_match_criteria_.filter_metadata().end() ||
        host_metadata_filter_it == host.metadata()->filter_metadata().end()) {
      return false;
    }

    const auto& match_criteria_fields = match_criteria_filter_it->second.fields();
    const auto& host_metadata_fields = host_metadata_filter_it->second.fields();

    for (auto const& element : match_criteria_fields) {
      auto entry_it = host_metadata_fields.find(element.first);
      if (entry_it != host_metadata_fields.end() &&
          ValueUtil::equal(element.second, entry_it->second)) {
        return true;
      }
    }
    return false;
  }

  void onHostAttempted(Upstream::HostDescriptionConstSharedPtr) override {}

private:
  std::vector<Upstream::HostDescription const*> attempted_hosts_;
  const envoy::api::v2::core::Metadata metadata_match_criteria_;
};
} // namespace Envoy