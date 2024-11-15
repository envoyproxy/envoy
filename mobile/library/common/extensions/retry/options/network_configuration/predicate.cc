#include "library/common/extensions/retry/options/network_configuration/predicate.h"

#include "library/common/stream_info/extra_stream_info.h"

namespace Envoy {
namespace Extensions {
namespace Retry {
namespace Options {

NetworkConfigurationRetryOptionsPredicate::NetworkConfigurationRetryOptionsPredicate(
    const envoymobile::extensions::retry::options::network_configuration::
        NetworkConfigurationOptionsPredicate&,
    Upstream::RetryExtensionFactoryContext& context) {
  connectivity_manager_ = Network::ConnectivityManagerHandle{context.singletonManager()}.get();
  RELEASE_ASSERT(connectivity_manager_ != nullptr,
                 "unexpected nullptr network connectivity_manager");
}

Upstream::RetryOptionsPredicate::UpdateOptionsReturn
NetworkConfigurationRetryOptionsPredicate::updateOptions(
    const Upstream::RetryOptionsPredicate::UpdateOptionsParameters& parameters) const {

  auto options = std::make_shared<Network::Socket::Options>();
  auto& stream_info = parameters.retriable_request_stream_info_;
  auto filter_state = stream_info.filterState();

  // ExtraStreamInfo is added by the NetworkConfigurationFilter and should normally always be
  // present - this check is mostly defensive.
  if (!filter_state->hasData<StreamInfo::ExtraStreamInfo>(StreamInfo::ExtraStreamInfo::key())) {
    ENVOY_LOG(warn, "extra stream info is missing");

    // Returning nullopt results in existing socket options being preserved.
    return Upstream::RetryOptionsPredicate::UpdateOptionsReturn{absl::nullopt};
  }

  StreamInfo::ExtraStreamInfo* extra_stream_info =
      filter_state->getDataMutable<StreamInfo::ExtraStreamInfo>(StreamInfo::ExtraStreamInfo::key());

  if (extra_stream_info == nullptr) {
    ENVOY_LOG(warn, "extra stream info is missing");

    // Returning nullopt results in existing socket options being preserved.
    return Upstream::RetryOptionsPredicate::UpdateOptionsReturn{absl::nullopt};
  }

  // This check is also defensive. The NetworkConfigurationFilter should always set this when
  // ExtraStreaminfo is created.
  if (!extra_stream_info->configuration_key_.has_value()) {
    ENVOY_LOG(warn, "network configuration key is missing");

    // Returning nullopt results in existing socket options being preserved.
    return Upstream::RetryOptionsPredicate::UpdateOptionsReturn{absl::nullopt};
  }

  // As a proxy for the many different types of network errors, this code interprets any failure
  // where a stream received no bytes from the upstream as a network fault. This status is passed to
  // the connectivity_manager below when we report network usage, where it may be factored into
  // future socket configuration.
  bool network_fault =
      !stream_info.upstreamInfo() ||
      !stream_info.upstreamInfo()->upstreamTiming().first_upstream_rx_byte_received_.has_value();

  // Report request status to network connectivity_manager, so that socket configuration may be
  // adapted to current network conditions.
  connectivity_manager_->reportNetworkUsage(extra_stream_info->configuration_key_.value(),
                                            network_fault);

  // Update socket configuration for next retry attempt.
  extra_stream_info->configuration_key_ = connectivity_manager_->addUpstreamSocketOptions(options);

  // The options returned here replace any existing socket options used for a prior attempt. At
  // present, all socket options set in Envoy Mobile are provided by the NetworkConnectivityManager,
  // so it's safe to simply replace them.
  // TODO(goaway): If additional socket options are ever provided by a source other than the
  // NetworkConnectivityManager, we need to account for the potential presence of those options
  // here.
  return Upstream::RetryOptionsPredicate::UpdateOptionsReturn{options};
}

} // namespace Options
} // namespace Retry
} // namespace Extensions
} // namespace Envoy
