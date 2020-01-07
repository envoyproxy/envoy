#include "extensions/retry/host/omit_host_metadata/omit_host_metadata.h"

#include "common/config/metadata.h"
#include "common/config/well_known_names.h"

namespace Envoy {
namespace Extensions {
namespace Retry {
namespace Host {

bool OmitHostsRetryPredicate::shouldSelectAnotherHost(const Upstream::Host& host) {
  // Check if host doesn't have any metadata.
  if (host.metadata()->filter_metadata().find(Envoy::Config::MetadataFilters::get().ENVOY_LB) ==
      host.metadata()->filter_metadata().end()) {
    return false;
  }

  // Check if there's no metadata match criteria configured.
  const auto& filter_it = metadata_match_criteria_.filter_metadata().find(
      Envoy::Config::MetadataFilters::get().ENVOY_LB);
  if (filter_it == metadata_match_criteria_.filter_metadata().end()) {
    return false;
  }

  std::vector<std::pair<std::string, ProtobufWkt::Value>> kv;
  for (auto const& it : filter_it->second.fields()) {
    kv.push_back(std::make_pair(it.first, it.second));
  }

  return Envoy::Config::Metadata::metadataLabelMatch(
      kv, *host.metadata(), Envoy::Config::MetadataFilters::get().ENVOY_LB, true);
}

} // namespace Host
} // namespace Retry
} // namespace Extensions
} // namespace Envoy
