#pragma once

#include "source/common/singleton/const_singleton.h"
#include "source/extensions/filters/common/expr/cel_state.h"
#include "source/extensions/filters/http/common/factory_base.h"
#include "source/extensions/filters/http/common/pass_through_filter.h"

#include "contrib/envoy/extensions/filters/http/peer_metadata/v3/peer_metadata.pb.h"
#include "contrib/envoy/extensions/filters/http/peer_metadata/v3/peer_metadata.pb.validate.h"
#include "contrib/istio/filters/common/source/metadata_object.h"
#include "contrib/istio/filters/common/source/workload_discovery.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace PeerMetadata {

using ::Envoy::Extensions::Filters::Common::Expr::CelStatePrototype;
using ::Envoy::Extensions::Filters::Common::Expr::CelStateType;

struct HeaderValues {
  const Http::LowerCaseString ExchangeMetadataHeader{"x-envoy-peer-metadata"};
  const Http::LowerCaseString ExchangeMetadataHeaderId{"x-envoy-peer-metadata-id"};
};

using Headers = ConstSingleton<HeaderValues>;

using PeerInfo = Istio::Common::WorkloadMetadataObject;

struct Context {
  bool request_peer_id_received_{false};
  bool request_peer_received_{false};
};

// Base class for the discovery methods. First derivation wins but all methods perform removal.
class DiscoveryMethod {
public:
  virtual ~DiscoveryMethod() = default;
  virtual absl::optional<PeerInfo> derivePeerInfo(const StreamInfo::StreamInfo&, Http::HeaderMap&,
                                                  Context&) const PURE;
  virtual void remove(Http::HeaderMap&) const {}
};

using DiscoveryMethodPtr = std::unique_ptr<DiscoveryMethod>;

class MXMethod : public DiscoveryMethod {
public:
  MXMethod(bool downstream, const absl::flat_hash_set<std::string> additional_labels,
           Server::Configuration::ServerFactoryContext& factory_context);
  absl::optional<PeerInfo> derivePeerInfo(const StreamInfo::StreamInfo&, Http::HeaderMap&,
                                          Context&) const override;
  void remove(Http::HeaderMap&) const override;

private:
  absl::optional<PeerInfo> lookup(absl::string_view id, absl::string_view value) const;
  const bool downstream_;
  struct MXCache : public ThreadLocal::ThreadLocalObject {
    absl::flat_hash_map<std::string, PeerInfo> cache_;
  };
  mutable ThreadLocal::TypedSlot<MXCache> tls_;
  const absl::flat_hash_set<std::string> additional_labels_;
  const int64_t max_peer_cache_size_{500};
};

// Base class for the propagation methods.
class PropagationMethod {
public:
  virtual ~PropagationMethod() = default;
  virtual void inject(const StreamInfo::StreamInfo&, Http::HeaderMap&, Context&) const PURE;
};

using PropagationMethodPtr = std::unique_ptr<PropagationMethod>;

class MXPropagationMethod : public PropagationMethod {
public:
  MXPropagationMethod(bool downstream, Server::Configuration::ServerFactoryContext& factory_context,
                      const absl::flat_hash_set<std::string>& additional_labels,
                      const io::istio::http::peer_metadata::Config_IstioHeaders&);
  void inject(const StreamInfo::StreamInfo&, Http::HeaderMap&, Context&) const override;

private:
  const bool downstream_;
  std::string computeValue(const absl::flat_hash_set<std::string>&,
                           Server::Configuration::ServerFactoryContext&) const;
  const std::string id_;
  const std::string value_;
  const bool skip_external_clusters_;
  bool skipMXHeaders(const bool, const StreamInfo::StreamInfo&) const;
};

class FilterConfig : public Logger::Loggable<Logger::Id::filter> {
public:
  FilterConfig(const io::istio::http::peer_metadata::Config&,
               Server::Configuration::FactoryContext&);
  void discoverDownstream(StreamInfo::StreamInfo&, Http::RequestHeaderMap&, Context&) const;
  void discoverUpstream(StreamInfo::StreamInfo&, Http::ResponseHeaderMap&, Context&) const;
  void injectDownstream(const StreamInfo::StreamInfo&, Http::ResponseHeaderMap&, Context&) const;
  void injectUpstream(const StreamInfo::StreamInfo&, Http::RequestHeaderMap&, Context&) const;

  static const CelStatePrototype& peerInfoPrototype() {
    static const CelStatePrototype* const prototype = new CelStatePrototype(
        true, CelStateType::Protobuf, "type.googleapis.com/google.protobuf.Struct",
        StreamInfo::FilterState::LifeSpan::FilterChain);
    return *prototype;
  }

private:
  std::vector<DiscoveryMethodPtr> buildDiscoveryMethods(
      const Protobuf::RepeatedPtrField<io::istio::http::peer_metadata::Config::DiscoveryMethod>&,
      const absl::flat_hash_set<std::string>& additional_labels, bool downstream,
      Server::Configuration::FactoryContext&) const;
  std::vector<PropagationMethodPtr> buildPropagationMethods(
      const Protobuf::RepeatedPtrField<io::istio::http::peer_metadata::Config::PropagationMethod>&,
      const absl::flat_hash_set<std::string>& additional_labels, bool downstream,
      Server::Configuration::FactoryContext&) const;
  absl::flat_hash_set<std::string>
  buildAdditionalLabels(const Protobuf::RepeatedPtrField<std::string>&) const;
  StreamInfo::StreamSharingMayImpactPooling sharedWithUpstream() const {
    return shared_with_upstream_
               ? StreamInfo::StreamSharingMayImpactPooling::SharedWithUpstreamConnectionOnce
               : StreamInfo::StreamSharingMayImpactPooling::None;
  }
  void discover(StreamInfo::StreamInfo&, bool downstream, Http::HeaderMap&, Context&) const;
  void setFilterState(StreamInfo::StreamInfo&, bool downstream, const PeerInfo& value) const;
  const bool shared_with_upstream_;
  const std::vector<DiscoveryMethodPtr> downstream_discovery_;
  const std::vector<DiscoveryMethodPtr> upstream_discovery_;
  const std::vector<PropagationMethodPtr> downstream_propagation_;
  const std::vector<PropagationMethodPtr> upstream_propagation_;
};

using FilterConfigSharedPtr = std::shared_ptr<FilterConfig>;

class Filter : public Http::PassThroughFilter {
public:
  Filter(const FilterConfigSharedPtr& config) : config_(config) {}
  Http::FilterHeadersStatus decodeHeaders(Http::RequestHeaderMap&, bool) override;
  Http::FilterHeadersStatus encodeHeaders(Http::ResponseHeaderMap&, bool) override;

private:
  FilterConfigSharedPtr config_;
  Context ctx_;
};

class FilterConfigFactory : public Server::Configuration::NamedHttpFilterConfigFactory {
public:
  std::string name() const override { return "envoy.filters.http.peer_metadata"; }

  ProtobufTypes::MessagePtr createEmptyConfigProto() override {
    return std::make_unique<io::istio::http::peer_metadata::Config>();
  }

  absl::StatusOr<Http::FilterFactoryCb>
  createFilterFactoryFromProto(const Protobuf::Message& proto_config, const std::string&,
                               Server::Configuration::FactoryContext&) override;
};

} // namespace PeerMetadata
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
