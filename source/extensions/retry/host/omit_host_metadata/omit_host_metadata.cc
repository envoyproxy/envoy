#include "extensions/retry/host/omit_host_metadata/omit_host_metadata.h"

#include "common/config/metadata.h"
#include "common/config/well_known_names.h"

namespace Envoy {
namespace Extensions {
namespace Retry {
namespace Host {

bool OmitHostsRetryPredicate::shouldSelectAnotherHost(const Upstream::Host& host) {
  return labelSet_.size() &&
         Envoy::Config::Metadata::metadataLabelMatch(
             labelSet_, *host.metadata(), Envoy::Config::MetadataFilters::get().ENVOY_LB, true);
}

} // namespace Host
} // namespace Retry
} // namespace Extensions
} // namespace Envoy
