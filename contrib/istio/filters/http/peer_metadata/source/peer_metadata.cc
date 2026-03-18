#include "contrib/istio/filters/http/peer_metadata/source/peer_metadata.h"

#include "envoy/registry/registry.h"
#include "envoy/server/factory_context.h"

#include "source/common/common/base64.h"
#include "source/common/common/hash.h"
#include "source/common/http/header_utility.h"
#include "source/common/http/utility.h"
#include "source/common/network/utility.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace PeerMetadata {

using ::Envoy::Extensions::Filters::Common::Expr::CelState;

class XDSMethod : public DiscoveryMethod {
public:
  XDSMethod(bool downstream, Server::Configuration::ServerFactoryContext& factory_context)
      : downstream_(downstream),
        metadata_provider_(Extensions::Common::WorkloadDiscovery::getProvider(factory_context)) {}
  absl::optional<PeerInfo> derivePeerInfo(const StreamInfo::StreamInfo&, Http::HeaderMap&,
                                          Context&) const override;

private:
  const bool downstream_;
  Extensions::Common::WorkloadDiscovery::WorkloadMetadataProviderSharedPtr metadata_provider_;
};

absl::optional<PeerInfo> XDSMethod::derivePeerInfo(const StreamInfo::StreamInfo& info,
                                                   Http::HeaderMap&, Context&) const {
  if (!metadata_provider_) {
    return {};
  }
  Network::Address::InstanceConstSharedPtr peer_address;
  if (downstream_) {
    peer_address = info.downstreamAddressProvider().remoteAddress();
  } else {
    if (info.upstreamInfo().has_value()) {
      auto upstream_host = info.upstreamInfo().value().get().upstreamHost();
      if (upstream_host) {
        const auto address = upstream_host->address();
        switch (address->type()) {
        case Network::Address::Type::Ip:
          peer_address = upstream_host->address();
          break;
        case Network::Address::Type::EnvoyInternal:
          if (upstream_host->metadata()) {
            const auto& filter_metadata = upstream_host->metadata()->filter_metadata();
            const auto& it = filter_metadata.find("envoy.filters.listener.original_dst");
            if (it != filter_metadata.end()) {
              const auto& destination_it = it->second.fields().find("local");
              if (destination_it != it->second.fields().end()) {
                peer_address = Network::Utility::parseInternetAddressAndPortNoThrow(
                    destination_it->second.string_value(), /*v6only=*/false);
              }
            }
          }
          break;
        default:
          break;
        }
      }
    }
  }
  if (!peer_address) {
    return {};
  }
  ENVOY_LOG_MISC(debug, "Peer address: {}", peer_address->asString());
  return metadata_provider_->getMetadata(peer_address);
}

MXMethod::MXMethod(bool downstream, const absl::flat_hash_set<std::string> additional_labels,
                   Server::Configuration::ServerFactoryContext& factory_context)
    : downstream_(downstream), tls_(factory_context.threadLocal()),
      additional_labels_(additional_labels) {
  tls_.set([](Event::Dispatcher&) { return std::make_shared<MXCache>(); });
}

absl::optional<PeerInfo> MXMethod::derivePeerInfo(const StreamInfo::StreamInfo&,
                                                  Http::HeaderMap& headers, Context& ctx) const {
  const auto peer_id_header = headers.get(Headers::get().ExchangeMetadataHeaderId);
  if (downstream_) {
    ctx.request_peer_id_received_ = !peer_id_header.empty();
  }
  absl::string_view peer_id =
      peer_id_header.empty() ? "" : peer_id_header[0]->value().getStringView();
  const auto peer_info_header = headers.get(Headers::get().ExchangeMetadataHeader);
  if (downstream_) {
    ctx.request_peer_received_ = !peer_info_header.empty();
  }
  absl::string_view peer_info =
      peer_info_header.empty() ? "" : peer_info_header[0]->value().getStringView();
  if (!peer_info.empty()) {
    return lookup(peer_id, peer_info);
  }
  return {};
}

void MXMethod::remove(Http::HeaderMap& headers) const {
  headers.remove(Headers::get().ExchangeMetadataHeaderId);
  headers.remove(Headers::get().ExchangeMetadataHeader);
}

absl::optional<PeerInfo> MXMethod::lookup(absl::string_view id, absl::string_view value) const {
  // This code is copied from:
  // https://github.com/istio/proxy/blob/release-1.18/extensions/metadata_exchange/plugin.cc#L116
  auto& cache = tls_->cache_;
  if (max_peer_cache_size_ > 0 && !id.empty()) {
    auto it = cache.find(id);
    if (it != cache.end()) {
      return it->second;
    }
  }
  const auto bytes = Base64::decodeWithoutPadding(value);
  Protobuf::Struct metadata;
  if (!metadata.ParseFromString(bytes)) {
    return {};
  }
  auto out = Istio::Common::convertStructToWorkloadMetadata(metadata, additional_labels_);
  if (max_peer_cache_size_ > 0 && !id.empty()) {
    // do not let the cache grow beyond max cache size.
    if (static_cast<uint32_t>(cache.size()) > max_peer_cache_size_) {
      cache.erase(cache.begin(), std::next(cache.begin(), max_peer_cache_size_ / 4));
    }
    cache.emplace(id, *out);
  }
  return *out;
}

MXPropagationMethod::MXPropagationMethod(
    bool downstream, Server::Configuration::ServerFactoryContext& factory_context,
    const absl::flat_hash_set<std::string>& additional_labels,
    const io::istio::http::peer_metadata::Config_IstioHeaders& istio_headers)
    : downstream_(downstream), id_(factory_context.localInfo().node().id()),
      value_(computeValue(additional_labels, factory_context)),
      skip_external_clusters_(istio_headers.skip_external_clusters()) {}

std::string MXPropagationMethod::computeValue(
    const absl::flat_hash_set<std::string>& additional_labels,
    Server::Configuration::ServerFactoryContext& factory_context) const {
  const auto obj = Istio::Common::convertStructToWorkloadMetadata(
      factory_context.localInfo().node().metadata(), additional_labels);
  const Protobuf::Struct metadata = Istio::Common::convertWorkloadMetadataToStruct(*obj);
  const std::string metadata_bytes = Istio::Common::serializeToStringDeterministic(metadata);
  return Base64::encode(metadata_bytes.data(), metadata_bytes.size());
}

void MXPropagationMethod::inject(const StreamInfo::StreamInfo& info, Http::HeaderMap& headers,
                                 Context& ctx) const {
  if (skipMXHeaders(skip_external_clusters_, info)) {
    return;
  }
  if (!downstream_ || ctx.request_peer_id_received_) {
    headers.setReference(Headers::get().ExchangeMetadataHeaderId, id_);
  }
  if (!downstream_ || ctx.request_peer_received_) {
    headers.setReference(Headers::get().ExchangeMetadataHeader, value_);
  }
}

FilterConfig::FilterConfig(const io::istio::http::peer_metadata::Config& config,
                           Server::Configuration::FactoryContext& factory_context)
    : shared_with_upstream_(config.shared_with_upstream()),
      downstream_discovery_(buildDiscoveryMethods(config.downstream_discovery(),
                                                  buildAdditionalLabels(config.additional_labels()),
                                                  true, factory_context)),
      upstream_discovery_(buildDiscoveryMethods(config.upstream_discovery(),
                                                buildAdditionalLabels(config.additional_labels()),
                                                false, factory_context)),
      downstream_propagation_(buildPropagationMethods(
          config.downstream_propagation(), buildAdditionalLabels(config.additional_labels()), true,
          factory_context)),
      upstream_propagation_(buildPropagationMethods(
          config.upstream_propagation(), buildAdditionalLabels(config.additional_labels()), false,
          factory_context)) {}

std::vector<DiscoveryMethodPtr> FilterConfig::buildDiscoveryMethods(
    const Protobuf::RepeatedPtrField<io::istio::http::peer_metadata::Config::DiscoveryMethod>&
        config,
    const absl::flat_hash_set<std::string>& additional_labels, bool downstream,
    Server::Configuration::FactoryContext& factory_context) const {
  std::vector<DiscoveryMethodPtr> methods;
  methods.reserve(config.size());
  for (const auto& method : config) {
    switch (method.method_specifier_case()) {
    case io::istio::http::peer_metadata::Config::DiscoveryMethod::MethodSpecifierCase::
        kWorkloadDiscovery:
      methods.push_back(
          std::make_unique<XDSMethod>(downstream, factory_context.serverFactoryContext()));
      break;
    case io::istio::http::peer_metadata::Config::DiscoveryMethod::MethodSpecifierCase::
        kIstioHeaders:
      methods.push_back(std::make_unique<MXMethod>(downstream, additional_labels,
                                                   factory_context.serverFactoryContext()));
      break;
    default:
      break;
    }
  }
  return methods;
}

std::vector<PropagationMethodPtr> FilterConfig::buildPropagationMethods(
    const Protobuf::RepeatedPtrField<io::istio::http::peer_metadata::Config::PropagationMethod>&
        config,
    const absl::flat_hash_set<std::string>& additional_labels, bool downstream,
    Server::Configuration::FactoryContext& factory_context) const {
  std::vector<PropagationMethodPtr> methods;
  methods.reserve(config.size());
  for (const auto& method : config) {
    switch (method.method_specifier_case()) {
    case io::istio::http::peer_metadata::Config::PropagationMethod::MethodSpecifierCase::
        kIstioHeaders:
      methods.push_back(
          std::make_unique<MXPropagationMethod>(downstream, factory_context.serverFactoryContext(),
                                                additional_labels, method.istio_headers()));
      break;
    default:
      break;
    }
  }
  return methods;
}

absl::flat_hash_set<std::string>
FilterConfig::buildAdditionalLabels(const Protobuf::RepeatedPtrField<std::string>& labels) const {
  absl::flat_hash_set<std::string> result;
  for (const auto& label : labels) {
    result.emplace(label);
  }
  return result;
}

void FilterConfig::discoverDownstream(StreamInfo::StreamInfo& info, Http::RequestHeaderMap& headers,
                                      Context& ctx) const {
  discover(info, true, headers, ctx);
}

void FilterConfig::discoverUpstream(StreamInfo::StreamInfo& info, Http::ResponseHeaderMap& headers,
                                    Context& ctx) const {
  discover(info, false, headers, ctx);
}

void FilterConfig::discover(StreamInfo::StreamInfo& info, bool downstream, Http::HeaderMap& headers,
                            Context& ctx) const {
  for (const auto& method : downstream ? downstream_discovery_ : upstream_discovery_) {
    const auto result = method->derivePeerInfo(info, headers, ctx);
    if (result) {
      setFilterState(info, downstream, *result);
      break;
    }
  }
  for (const auto& method : downstream ? downstream_discovery_ : upstream_discovery_) {
    method->remove(headers);
  }
}

void FilterConfig::injectDownstream(const StreamInfo::StreamInfo& info,
                                    Http::ResponseHeaderMap& headers, Context& ctx) const {
  for (const auto& method : downstream_propagation_) {
    method->inject(info, headers, ctx);
  }
}

void FilterConfig::injectUpstream(const StreamInfo::StreamInfo& info,
                                  Http::RequestHeaderMap& headers, Context& ctx) const {
  for (const auto& method : upstream_propagation_) {
    method->inject(info, headers, ctx);
  }
}

void FilterConfig::setFilterState(StreamInfo::StreamInfo& info, bool downstream,
                                  const PeerInfo& value) const {
  const absl::string_view key =
      downstream ? Istio::Common::DownstreamPeer : Istio::Common::UpstreamPeer;
  if (!info.filterState()->hasDataWithName(key)) {
    // Use CelState to allow operation filter_state.upstream_peer.labels['role']
    auto pb = value.serializeAsProto();
    auto peer_info = std::make_unique<CelState>(FilterConfig::peerInfoPrototype());
    peer_info->setValue(absl::string_view(pb->SerializeAsString()));
    info.filterState()->setData(
        key, std::move(peer_info), StreamInfo::FilterState::StateType::Mutable,
        StreamInfo::FilterState::LifeSpan::FilterChain, sharedWithUpstream());
  } else {
    ENVOY_LOG(debug, "Duplicate peer metadata, skipping");
  }
}

Http::FilterHeadersStatus Filter::decodeHeaders(Http::RequestHeaderMap& headers, bool) {
  config_->discoverDownstream(decoder_callbacks_->streamInfo(), headers, ctx_);
  config_->injectUpstream(decoder_callbacks_->streamInfo(), headers, ctx_);
  return Http::FilterHeadersStatus::Continue;
}

bool MXPropagationMethod::skipMXHeaders(const bool skip_external_clusters,
                                        const StreamInfo::StreamInfo& info) const {
  // We skip metadata in two cases.
  // 1. skip_external_clusters is enabled, and we detect the upstream as external.
  const auto& cluster_info = info.upstreamClusterInfo();
  if (cluster_info && cluster_info.value()) {
    const auto& cluster_name = cluster_info.value()->name();
    // PassthroughCluster is always considered external
    if (skip_external_clusters && cluster_name == "PassthroughCluster") {
      return true;
    }
    const auto& filter_metadata = cluster_info.value()->metadata().filter_metadata();
    const auto& it = filter_metadata.find("istio");
    // Otherwise, cluster must be tagged as external
    if (it != filter_metadata.end()) {
      if (skip_external_clusters) {
        const auto& skip_mx = it->second.fields().find("external");
        if (skip_mx != it->second.fields().end()) {
          if (skip_mx->second.bool_value()) {
            return true;
          }
        }
      }
      const auto& skip_mx = it->second.fields().find("disable_mx");
      if (skip_mx != it->second.fields().end()) {
        if (skip_mx->second.bool_value()) {
          return true;
        }
      }
    }
  }
  return false;
}

Http::FilterHeadersStatus Filter::encodeHeaders(Http::ResponseHeaderMap& headers, bool) {
  config_->discoverUpstream(decoder_callbacks_->streamInfo(), headers, ctx_);
  config_->injectDownstream(decoder_callbacks_->streamInfo(), headers, ctx_);
  return Http::FilterHeadersStatus::Continue;
}

absl::StatusOr<Http::FilterFactoryCb> FilterConfigFactory::createFilterFactoryFromProto(
    const Protobuf::Message& config, const std::string&,
    Server::Configuration::FactoryContext& factory_context) {
  auto filter_config = std::make_shared<FilterConfig>(
      dynamic_cast<const io::istio::http::peer_metadata::Config&>(config), factory_context);
  return [filter_config](Http::FilterChainFactoryCallbacks& callbacks) {
    auto filter = std::make_shared<Filter>(filter_config);
    callbacks.addStreamFilter(filter);
  };
}

REGISTER_FACTORY(FilterConfigFactory, Server::Configuration::NamedHttpFilterConfigFactory);

} // namespace PeerMetadata
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
