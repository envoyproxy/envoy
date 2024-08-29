#include "library/common/extensions/filters/http/network_configuration/filter.h"

#include "envoy/server/filter_config.h"

#include "source/common/common/assert.h"
#include "source/common/common/thread.h"
#include "source/common/http/headers.h"
#include "source/common/http/utility.h"
#include "source/common/network/filter_state_proxy_info.h"

#include "library/common/api/external.h"
#include "library/common/bridge//utility.h"
#include "library/common/http/header_utility.h"
#include "library/common/types/c_types.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace NetworkConfiguration {

const Http::LowerCaseString AuthorityHeaderName{":authority"};

void NetworkConfigurationFilter::setDecoderFilterCallbacks(
    Http::StreamDecoderFilterCallbacks& callbacks) {
  ENVOY_LOG(debug, "NetworkConfigurationFilter::setDecoderFilterCallbacks");

  auto new_extra_stream_info = std::make_unique<StreamInfo::ExtraStreamInfo>();
  extra_stream_info_ = new_extra_stream_info.get();

  decoder_callbacks_ = &callbacks;
  decoder_callbacks_->streamInfo().filterState()->setData(
      StreamInfo::ExtraStreamInfo::key(), std::move(new_extra_stream_info),
      StreamInfo::FilterState::StateType::Mutable, StreamInfo::FilterState::LifeSpan::Request);

  auto options = std::make_shared<Network::Socket::Options>();
  connectivity_manager_->setInterfaceBindingEnabled(enable_interface_binding_);
  connectivity_manager_->setDrainPostDnsRefreshEnabled(enable_drain_post_dns_refresh_);
  extra_stream_info_->configuration_key_ = connectivity_manager_->addUpstreamSocketOptions(options);
  decoder_callbacks_->addUpstreamSocketOptions(options);
}

void NetworkConfigurationFilter::onLoadDnsCacheComplete(
    const Common::DynamicForwardProxy::DnsHostInfoSharedPtr& host_info) {
  if (onAddressResolved(host_info)) {
    continue_decoding_callback_ = decoder_callbacks_->dispatcher().createSchedulableCallback(
        [this]() { decoder_callbacks_->continueDecoding(); });
    continue_decoding_callback_->scheduleCallbackNextIteration();
    return;
  }
}

bool NetworkConfigurationFilter::onAddressResolved(
    const Common::DynamicForwardProxy::DnsHostInfoSharedPtr& host_info) {
  if (host_info->address()) {
    setInfo(decoder_callbacks_->streamInfo().getRequestHeaders()->getHostValue(),
            host_info->address());
    return true;
  }
  decoder_callbacks_->sendLocalReply(Http::Code::BadRequest,
                                     "Proxy configured but DNS resolution failed", nullptr,
                                     absl::nullopt, "no_dns_address_for_proxy");
  return false;
}

void NetworkConfigurationFilter::onProxyResolutionComplete(
    Network::ProxySettingsConstSharedPtr proxy_settings) {
  if (continueWithProxySettings(proxy_settings) == Http::FilterHeadersStatus::Continue) {
    // PAC URL resolution happens via Apple APIs on a separate thread (Apple's run loop thread), so
    // we schedule the continuation callback on the engine's dispatcher.
    continue_decoding_callback_ = decoder_callbacks_->dispatcher().createSchedulableCallback(
        [this]() { decoder_callbacks_->continueDecoding(); });
    continue_decoding_callback_->scheduleCallbackNextIteration();
  }
}

Http::FilterHeadersStatus
NetworkConfigurationFilter::decodeHeaders(Http::RequestHeaderMap& request_headers, bool) {
  ENVOY_LOG(trace, "NetworkConfigurationFilter::decodeHeaders: {}", request_headers);

  const auto authority = request_headers.getHostValue();
  if (authority.empty()) {
    return Http::FilterHeadersStatus::Continue;
  }

  // For iOS, we use the `envoy_proxy_resolver` API, which reads proxy settings asynchronously,
  // and callbacks are invoked in the filter when proxy settings are updated.
  auto* proxy_resolver = static_cast<Network::ProxyResolverApi*>(
      Api::External::retrieveApi("envoy_proxy_resolver", /*allow_absent=*/true));
  if (proxy_resolver != nullptr) {
    return resolveProxy(request_headers, proxy_resolver);
  }

  // For Android, proxy settings are pushed to the ConnectivityManager and synchronously handled
  // here.
  const auto proxy_settings = connectivity_manager_->getProxySettings();
  return continueWithProxySettings(proxy_settings);
}

Http::FilterHeadersStatus
NetworkConfigurationFilter::resolveProxy(Http::RequestHeaderMap& request_headers,
                                         Network::ProxyResolverApi* proxy_resolver) {
  RELEASE_ASSERT(Thread::MainThread::isMainOrTestThread(),
                 "NetworkConfigurationProxy::resolveProxy not running on main thread.");
  ASSERT(proxy_resolver != nullptr, "proxy_resolver must not be null.");

  const std::string target_url = Http::Utility::buildOriginalUri(request_headers, absl::nullopt);

  std::weak_ptr<NetworkConfigurationFilter> weak_self = weak_from_this();
  Network::ProxyResolutionResult proxy_resolution_result = proxy_resolver->resolver->resolveProxy(
      target_url, proxy_settings_,
      [&weak_self](const std::vector<Network::ProxySettings>& proxies) {
        RELEASE_ASSERT(
            Thread::MainThread::isMainOrTestThread(),
            "NetworkConfigurationProxy PAC proxy resolver callback not running on main thread.");
        // This is the callback invoked from the Apple APIs resolving the PAC file URL, which
        // gets invoked on the Envoy thread. We keep a weak_ptr to this filter instance
        // so that, if the stream is canceled and the filter chain is torn down in the meantime,
        // we will fail to aquire the weak_ptr lock and won't execute any callbacks on the resolved
        // proxies.
        if (auto filter_ptr = weak_self.lock()) {
          filter_ptr->onProxyResolutionComplete(Network::ProxySettings::create(proxies));
        }
      });

  switch (proxy_resolution_result) {
  case Network::ProxyResolutionResult::NoProxyConfigured:
    return Http::FilterHeadersStatus::Continue;
  case Network::ProxyResolutionResult::ResultCompleted:
    return continueWithProxySettings(Network::ProxySettings::create(proxy_settings_));
  case Network::ProxyResolutionResult::ResultInProgress:
    // `onProxyResolutionComplete` method will be called once the proxy resolution completes
    return Http::FilterHeadersStatus::StopAllIterationAndWatermark;
  }
}

Http::FilterHeadersStatus NetworkConfigurationFilter::continueWithProxySettings(
    Network::ProxySettingsConstSharedPtr proxy_settings) {
  // If there is no proxy configured, continue.
  if (proxy_settings == nullptr) {
    return Http::FilterHeadersStatus::Continue;
  }

  ENVOY_LOG(trace, "netconf_filter_processing_proxy_for_request proxy_settings={}",
            proxy_settings->asString());
  // If there is a proxy with a raw address, set the information, and continue.
  const auto proxy_address = proxy_settings->address();
  if (proxy_address != nullptr) {
    const auto host_value = decoder_callbacks_->streamInfo().getRequestHeaders()->getHostValue();
    setInfo(host_value, proxy_address);
    return Http::FilterHeadersStatus::Continue;
  }

  // If there's no address or hostname, continue.
  if (proxy_settings->hostname().empty()) {
    return Http::FilterHeadersStatus::Continue;
  }

  // If there's a proxy hostname but no way to do a DNS lookup, fail the request.
  if (!connectivity_manager_->dnsCache()) {
    decoder_callbacks_->sendLocalReply(Http::Code::BadRequest,
                                       "Proxy configured but no DNS cache available", nullptr,
                                       absl::nullopt, "no_dns_cache_for_proxy");
    return Http::FilterHeadersStatus::StopIteration;
  }

  // Attempt to load the proxy's hostname from the DNS cache.
  auto result = connectivity_manager_->dnsCache()->loadDnsCacheEntry(
      proxy_settings->hostname(), proxy_settings->port(), false, *this);

  // If the hostname is not in the cache, pause filter iteration. The DNS cache will call
  // onLoadDnsCacheComplete when DNS resolution succeeds, fails, or times out and processing
  // will resume from there.
  if (result.status_ == Common::DynamicForwardProxy::DnsCache::LoadDnsCacheEntryStatus::Loading) {
    dns_cache_handle_ = std::move(result.handle_);
    return Http::FilterHeadersStatus::StopAllIterationAndWatermark;
  }

  // If the hostname is in cache, set the info and continue.
  if (result.host_info_.has_value()) {
    if (onAddressResolved(*result.host_info_)) {
      return Http::FilterHeadersStatus::Continue;
    } else {
      return Http::FilterHeadersStatus::StopIteration;
    }
  }

  // If DNS lookup straight up fails, fail the request.
  decoder_callbacks_->sendLocalReply(Http::Code::BadRequest,
                                     "Proxy configured but DNS resolution failed", nullptr,
                                     absl::nullopt, "no_dns_address_for_proxy");
  return Http::FilterHeadersStatus::StopIteration;
}

void NetworkConfigurationFilter::setInfo(absl::string_view authority,
                                         Network::Address::InstanceConstSharedPtr address) {
  ENVOY_LOG(trace, "netconf_filter_set_proxy_for_request {} {}", authority, address->asString());
  decoder_callbacks_->streamInfo().filterState()->setData(
      Network::Http11ProxyInfoFilterState::key(),
      std::make_unique<Network::Http11ProxyInfoFilterState>(authority, address),
      StreamInfo::FilterState::StateType::ReadOnly, StreamInfo::FilterState::LifeSpan::FilterChain);
}

Http::FilterHeadersStatus NetworkConfigurationFilter::encodeHeaders(Http::ResponseHeaderMap&,
                                                                    bool) {
  ENVOY_LOG(debug, "NetworkConfigurationFilter::encodeHeaders");
  // Report request status to network connectivity_manager, so that socket configuration may be
  // adapted to current network conditions. Receiving headers from upstream always means some level
  // of network transmission was successful, so we unconditionally set network_fault to false.
  connectivity_manager_->reportNetworkUsage(extra_stream_info_->configuration_key_.value(),
                                            false /* network_fault */);

  return Http::FilterHeadersStatus::Continue;
}

Http::LocalErrorStatus NetworkConfigurationFilter::onLocalReply(const LocalReplyData& reply) {
  ENVOY_LOG(debug, "NetworkConfigurationFilter::onLocalReply");

  bool success_status = static_cast<int>(reply.code_) < 400;
  // Envoy uses local replies to report various local errors, including networking failures (which
  // Envoy Mobile later surfaces as errors). As a proxy for the many different types of network
  // errors, this code interprets any local error where a stream received no bytes from the upstream
  // as a network fault. This status is passed to the connectivity_manager below when we report
  // network usage, where it may be factored into future socket configuration.
  bool network_fault = !success_status && (!decoder_callbacks_->streamInfo().upstreamInfo() ||
                                           !decoder_callbacks_->streamInfo()
                                                .upstreamInfo()
                                                ->upstreamTiming()
                                                .first_upstream_rx_byte_received_.has_value());
  // Report request status to network connectivity_manager, so that socket configuration may be
  // adapted to current network conditions.
  connectivity_manager_->reportNetworkUsage(extra_stream_info_->configuration_key_.value(),
                                            network_fault);

  return Http::Utility::statusForOnLocalReply(reply, decoder_callbacks_->streamInfo());
}

void NetworkConfigurationFilter::onDestroy() { dns_cache_handle_.reset(); }

} // namespace NetworkConfiguration
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
