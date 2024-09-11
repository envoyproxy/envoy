#include "source/extensions/filters/udp/udp_proxy/session_filters/dynamic_forward_proxy/proxy_filter.h"

#include "envoy/router/string_accessor.h"
#include "envoy/stream_info/uint32_accessor.h"
#include "envoy/upstream/thread_local_cluster.h"

#include "source/common/common/assert.h"
#include "source/common/stream_info/uint32_accessor_impl.h"

namespace Envoy {
namespace Extensions {
namespace UdpFilters {
namespace UdpProxy {
namespace SessionFilters {
namespace DynamicForwardProxy {

constexpr uint32_t DefaultMaxBufferedDatagrams = 1024;
constexpr uint64_t DefaultMaxBufferedBytes = 16384;

ProxyFilterConfig::ProxyFilterConfig(
    const FilterConfig& config,
    Extensions::Common::DynamicForwardProxy::DnsCacheManagerFactory& cache_manager_factory,
    Server::Configuration::FactoryContext& context)
    : dns_cache_manager_(cache_manager_factory.get()),
      stats_scope_(context.scope().createScope(
          absl::StrCat("udp.session.dynamic_forward_proxy.", config.stat_prefix(), "."))),
      filter_stats_(generateStats(*stats_scope_)), buffer_enabled_(config.has_buffer_options()),
      max_buffered_datagrams_(config.has_buffer_options()
                                  ? PROTOBUF_GET_WRAPPED_OR_DEFAULT(config.buffer_options(),
                                                                    max_buffered_datagrams,
                                                                    DefaultMaxBufferedDatagrams)
                                  : DefaultMaxBufferedDatagrams),
      max_buffered_bytes_(config.has_buffer_options()
                              ? PROTOBUF_GET_WRAPPED_OR_DEFAULT(config.buffer_options(),
                                                                max_buffered_bytes,
                                                                DefaultMaxBufferedBytes)
                              : DefaultMaxBufferedBytes) {
  auto cache_or_error = dns_cache_manager_->getCache(config.dns_cache_config());
  THROW_IF_NOT_OK_REF(cache_or_error.status());
  dns_cache_ = std::move(cache_or_error.value());
}

ReadFilterStatus ProxyFilter::onNewSession() {
  absl::string_view host;
  const auto* host_filter_state =
      read_callbacks_->streamInfo().filterState()->getDataReadOnly<Router::StringAccessor>(
          "envoy.upstream.dynamic_host");
  if (host_filter_state != nullptr) {
    host = host_filter_state->asString();
  }

  absl::optional<uint32_t> port;
  const auto* port_filter_state =
      read_callbacks_->streamInfo().filterState()->getDataReadOnly<StreamInfo::UInt32Accessor>(
          "envoy.upstream.dynamic_port");
  if (port_filter_state != nullptr && port_filter_state->value() > 0 &&
      port_filter_state->value() <= 65535) {
    port = port_filter_state->value();
  }

  if (host.empty() || !port.has_value()) {
    ENVOY_LOG(trace, "new session missing host or port");
    // TODO(ohadvano): add callback to remove session.
    return ReadFilterStatus::StopIteration;
  }

  ENVOY_LOG(trace, "new session with host '{}', port '{}'", host, port.value());

  circuit_breaker_ = config_->cache().canCreateDnsRequest();
  if (circuit_breaker_ == nullptr) {
    ENVOY_LOG(debug, "pending request overflow");
    // TODO(ohadvano): add callback to remove session.
    return ReadFilterStatus::StopIteration;
  }

  auto result = config_->cache().loadDnsCacheEntry(host, port.value(), false, *this);

  cache_load_handle_ = std::move(result.handle_);
  if (cache_load_handle_ == nullptr) {
    circuit_breaker_.reset();
  }

  switch (result.status_) {
  case LoadDnsCacheEntryStatus::InCache:
    ASSERT(cache_load_handle_ == nullptr);
    ENVOY_LOG(debug, "DNS cache entry already loaded, continuing");
    load_dns_cache_completed_ = true;
    return ReadFilterStatus::Continue;
  case LoadDnsCacheEntryStatus::Loading:
    ASSERT(cache_load_handle_ != nullptr);
    ENVOY_LOG(debug, "waiting to load DNS cache entry");
    return ReadFilterStatus::StopIteration;
  case LoadDnsCacheEntryStatus::Overflow:
    ASSERT(cache_load_handle_ == nullptr);
    ENVOY_LOG(debug, "DNS cache overflow");
    // TODO(ohadvano): add callback to remove session.
    return ReadFilterStatus::StopIteration;
  }

  PANIC_DUE_TO_CORRUPT_ENUM;
}

ReadFilterStatus ProxyFilter::onData(Network::UdpRecvData& data) {
  if (load_dns_cache_completed_) {
    return ReadFilterStatus::Continue;
  }

  maybeBufferDatagram(data);
  return ReadFilterStatus::StopIteration;
}

void ProxyFilter::onLoadDnsCacheComplete(
    const Common::DynamicForwardProxy::DnsHostInfoSharedPtr& host_info) {
  ENVOY_LOG(debug, "load DNS cache complete, continuing");
  if (!host_info || !host_info->address()) {
    ENVOY_LOG(debug, "empty DNS respose received");
  }

  ASSERT(circuit_breaker_ != nullptr);
  circuit_breaker_.reset();

  load_dns_cache_completed_ = true;

  if (!read_callbacks_->continueFilterChain()) {
    return;
  }

  while (!datagrams_buffer_.empty()) {
    BufferedDatagramPtr buffered_datagram = std::move(datagrams_buffer_.front());
    datagrams_buffer_.pop();
    read_callbacks_->injectDatagramToFilterChain(*buffered_datagram);
  }

  config_->disableBuffer();
  buffered_bytes_ = 0;
}

void ProxyFilter::maybeBufferDatagram(Network::UdpRecvData& data) {
  if (!config_->bufferEnabled()) {
    return;
  }

  if (datagrams_buffer_.size() == config_->maxBufferedDatagrams() ||
      buffered_bytes_ + data.buffer_->length() > config_->maxBufferedBytes()) {
    config_->filterStats().buffer_overflow_.inc();
    return;
  }

  auto buffered_datagram = std::make_unique<Network::UdpRecvData>();
  buffered_datagram->addresses_ = {std::move(data.addresses_.local_),
                                   std::move(data.addresses_.peer_)};
  buffered_datagram->buffer_ = std::move(data.buffer_);
  buffered_datagram->receive_time_ = data.receive_time_;
  buffered_bytes_ += buffered_datagram->buffer_->length();
  datagrams_buffer_.push(std::move(buffered_datagram));
}

} // namespace DynamicForwardProxy
} // namespace SessionFilters
} // namespace UdpProxy
} // namespace UdpFilters
} // namespace Extensions
} // namespace Envoy
