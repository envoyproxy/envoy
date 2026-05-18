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
        metadata_provider_(Extensions::Common::WorkloadDiscovery::getProvider(factory_context)),
        local_info_(factory_context.localInfo()) {}
  absl::optional<PeerInfo> derivePeerInfo(const StreamInfo::StreamInfo&, Http::HeaderMap&,
                                          Context&) const override;

private:
  const bool downstream_;
  Extensions::Common::WorkloadDiscovery::WorkloadMetadataProviderSharedPtr metadata_provider_;
  const LocalInfo::LocalInfo& local_info_;
};

absl::optional<PeerInfo> XDSMethod::derivePeerInfo(const StreamInfo::StreamInfo& info,
                                                   Http::HeaderMap& headers, Context&) const {
  if (!metadata_provider_) {
    return {};
  }
  Network::Address::InstanceConstSharedPtr peer_address;
  if (downstream_) {
    const auto origin_network_header = headers.get(Headers::get().ExchangeMetadataOriginNetwork);
    const auto& local_metadata = local_info_.node().metadata();
    const auto& it = local_metadata.fields().find("NETWORK");
    // We might not have a local network configured in the single cluster case, so default to empty.
    auto local_network = it != local_metadata.fields().end() ? it->second.string_value() : "";
    if (!origin_network_header.empty() &&
        origin_network_header[0]->value().getStringView() != local_network) {
      ENVOY_LOG_MISC(debug,
                     "Origin network header present: {}; skipping downstream workload discovery",
                     origin_network_header[0]->value().getStringView());
      peer_address = {};
    } else {
      peer_address = info.downstreamAddressProvider().remoteAddress();
    }
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
            const auto& istio_it = filter_metadata.find("istio");
            if (istio_it != filter_metadata.end()) {
              const auto& double_hbone_it = istio_it->second.fields().find("double_hbone");
              // This is an E/W gateway endpoint, so we should explicitly not use workload discovery
              if (double_hbone_it != istio_it->second.fields().end()) {
                ENVOY_LOG_MISC(
                    debug,
                    "Skipping upstream workload discovery for an endpoint on a remote network");
                peer_address = nullptr;
                break;
              }
            } else {
              ENVOY_LOG_MISC(debug, "No istio metadata found on upstream host.");
            }
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
      factory_context.localInfo().node().metadata(), additional_labels,
      factory_context.localInfo().node().locality());
  const Protobuf::Struct metadata = Istio::Common::convertWorkloadMetadataToStruct(*obj);
  const std::string metadata_bytes = Istio::Common::serializeToStringDeterministic(metadata);
  return Base64::encode(metadata_bytes.data(), metadata_bytes.size());
}

class UpstreamFilterStateMethod : public DiscoveryMethod {
public:
  UpstreamFilterStateMethod(
      const io::istio::http::peer_metadata::Config_UpstreamFilterState& config)
      : peer_metadata_key_(config.peer_metadata_key()) {}
  absl::optional<PeerInfo> derivePeerInfo(const StreamInfo::StreamInfo&, Http::HeaderMap&,
                                          Context&) const override;

private:
  std::string peer_metadata_key_;
};

absl::optional<PeerInfo>
UpstreamFilterStateMethod::derivePeerInfo(const StreamInfo::StreamInfo& info, Http::HeaderMap&,
                                          Context&) const {
  const auto upstream = info.upstreamInfo();
  if (!upstream) {
    return {};
  }

  const auto filter_state = upstream->upstreamFilterState();
  if (!filter_state) {
    return {};
  }

  const auto* cel_state =
      filter_state->getDataReadOnly<Envoy::Extensions::Filters::Common::Expr::CelState>(
          peer_metadata_key_);
  if (!cel_state) {
    return {};
  }

  Protobuf::Struct obj;
  if (!obj.ParseFromString(absl::string_view(cel_state->value()))) {
    return {};
  }

  std::unique_ptr<PeerInfo> peer_info = Istio::Common::convertStructToWorkloadMetadata(obj);
  if (!peer_info) {
    return {};
  }

  return *peer_info;
}

BaggagePropagationMethod::BaggagePropagationMethod(
    Server::Configuration::ServerFactoryContext& factory_context,
    const io::istio::http::peer_metadata::Config_Baggage&)
    : value_(computeBaggageValue(factory_context)) {}

std::string BaggagePropagationMethod::computeBaggageValue(
    Server::Configuration::ServerFactoryContext& factory_context) const {
  const auto obj = Istio::Common::convertStructToWorkloadMetadata(
      factory_context.localInfo().node().metadata(), {},
      factory_context.localInfo().node().locality());
  return obj->baggage();
}

void BaggagePropagationMethod::inject(const StreamInfo::StreamInfo&, Http::HeaderMap& headers,
                                      Context&) const {
  headers.setReference(Headers::get().Baggage, value_);
}

BaggageDiscoveryMethod::BaggageDiscoveryMethod() = default;

absl::optional<PeerInfo> BaggageDiscoveryMethod::derivePeerInfo(const StreamInfo::StreamInfo&,
                                                                Http::HeaderMap& headers,
                                                                Context&) const {
  const auto baggage_header = headers.get(Headers::get().Baggage);
  if (baggage_header.empty()) {
    return {};
  }
  const auto baggage_value = baggage_header[0]->value().getStringView();
  const auto workload = Istio::Common::convertBaggageToWorkloadMetadata(baggage_value);
  if (workload) {
    return *workload;
  }
  return {};
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
    case io::istio::http::peer_metadata::Config::DiscoveryMethod::MethodSpecifierCase::kBaggage:
      if (downstream) {
        methods.push_back(std::make_unique<BaggageDiscoveryMethod>());
      } else {
        ENVOY_LOG(warn, "BaggageDiscovery peer metadata discovery option is only available for "
                        "downstream peer discovery");
      }
      break;
    case io::istio::http::peer_metadata::Config::DiscoveryMethod::MethodSpecifierCase::
        kUpstreamFilterState:
      if (!downstream) {
        methods.push_back(
            std::make_unique<UpstreamFilterStateMethod>(method.upstream_filter_state()));
      } else {
        ENVOY_LOG(warn, "UpstreamFilterState peer metadata discovery option is only available for "
                        "upstream peer discovery");
      }
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
    case io::istio::http::peer_metadata::Config::PropagationMethod::MethodSpecifierCase::kBaggage:
      methods.push_back(std::make_unique<BaggagePropagationMethod>(
          factory_context.serverFactoryContext(), method.baggage()));
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
  const absl::string_view obj_key =
      downstream ? Istio::Common::DownstreamPeerObj : Istio::Common::UpstreamPeerObj;
  if (!info.filterState()->hasDataWithName(key)) {
    // Store CelState for CEL expressions like filter_state.downstream_peer.labels['role']
    auto pb = value.serializeAsProto();
    auto cel_state = std::make_unique<CelState>(FilterConfig::peerInfoPrototype());
    cel_state->setValue(absl::string_view(pb->SerializeAsString()));
    info.filterState()->setData(
        key, std::move(cel_state), StreamInfo::FilterState::StateType::Mutable,
        StreamInfo::FilterState::LifeSpan::FilterChain, sharedWithUpstream());

    // Also store WorkloadMetadataObject under a separate key for FIELD accessor support.
    // WorkloadMetadataObject implements hasFieldSupport() + getField() for
    // formatters using %FILTER_STATE(downstream_peer_obj:FIELD:fieldname)% syntax.
    auto workload_metadata = std::make_unique<PeerInfo>(value);
    info.filterState()->setData(
        obj_key, std::move(workload_metadata), StreamInfo::FilterState::StateType::Mutable,
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
  const auto cluster_info = info.upstreamClusterInfo();
  if (cluster_info) {
    const auto& cluster_name = cluster_info->name();
    // PassthroughCluster is always considered external
    if (skip_external_clusters && cluster_name == "PassthroughCluster") {
      return true;
    }
    const auto& filter_metadata = cluster_info->metadata().filter_metadata();
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
