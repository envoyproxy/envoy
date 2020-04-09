#include "extensions/retry/host/omit_host_metadata/omit_host_metadata.h"

#include "common/config/metadata.h"
#include "common/config/well_known_names.h"

namespace Envoy {
namespace Extensions {
namespace Retry {
namespace Host {

bool OmitHostsRetryPredicate::shouldSelectAnotherHost(const Upstream::Host& host) {
  // Note: The additional check to verify if the labelSet is empty is performed since
  // metadataLabelMatch returns true in case of an empty labelSet. However, for an empty labelSet,
  // i.e. if there is no matching criteria defined, this method should return false.
  return !labelSet_.empty() && Envoy::Config::Metadata::metadataLabelMatch(
                                   labelSet_, host.metadata().get(),
                                   Envoy::Config::MetadataFilters::get().ENVOY_LB, true);
}

} // namespace Host
} // namespace Retry
} // namespace Extensions
} // namespace Envoy
