#include "contrib/istio/filters/network/peer_metadata/source/peer_metadata.h"

#include <optional>

#include "envoy/stream_info/uint64_accessor.h"

#include "source/common/buffer/buffer_impl.h"
#include "source/common/router/string_accessor_impl.h"
#include "source/common/stream_info/bool_accessor_impl.h"
#include "source/common/stream_info/uint64_accessor_impl.h"
#include "source/common/tcp_proxy/tcp_proxy.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace PeerMetadata {

namespace {

using CelState = ::Envoy::Extensions::Filters::Common::Expr::CelState;
using CelStateType = ::Envoy::Extensions::Filters::Common::Expr::CelStateType;

std::string baggageValue(const LocalInfo::LocalInfo& local_info) {
  const auto obj = ::Istio::Common::convertStructToWorkloadMetadata(local_info.node().metadata());
  return obj->baggage();
}

std::optional<absl::string_view> internalListenerName(const Network::Address::Instance& address) {
  const auto internal = address.envoyInternalAddress();
  if (internal != nullptr) {
    return internal->addressId();
  }
  return std::nullopt;
}

bool allowedInternalListener(const Network::Address::Instance& address) {
  const auto name = internalListenerName(address);
  if (!name) {
    return false;
  }
  // internal_outbound is a listener name used in proxy e2e tests, so we allow it here as well.
  return *name == "connect_originate" || *name == "inner_connect_originate" ||
         *name == "internal_outbound";
}

bool discoveryDisabled(const ::envoy::config::core::v3::Metadata& metadata) {
  const auto& value = ::Envoy::Config::Metadata::metadataValue(
      &metadata, FilterNames::get().Name, FilterNames::get().DisableDiscoveryField);
  return value.bool_value();
}

} // namespace

const uint32_t PeerMetadataHeader::magic_number = 0xabcd1234;

Filter::Filter(const Config& config, const LocalInfo::LocalInfo& local_info,
               Filters::Common::PeerMetadataShared::PeerMetadataRegistrySharedPtr registry)
    : config_(config), baggage_(baggageValue(local_info)), registry_(std::move(registry)) {}

Network::FilterStatus Filter::onData(Buffer::Instance&, bool) {
  return Network::FilterStatus::Continue;
}

Network::FilterStatus Filter::onNewConnection() {
  ENVOY_LOG(trace, "New connection from downstream");
  populateBaggage();
  if (disableDiscovery()) {
    state_ = PeerMetadataState::PassThrough;
    ENVOY_LOG(trace, "Peer metadata discovery disabled via metadata");
  }
  return Network::FilterStatus::Continue;
}

void Filter::initializeReadFilterCallbacks(Network::ReadFilterCallbacks& callbacks) {
  read_callbacks_ = &callbacks;
}

Network::FilterStatus Filter::onWrite(Buffer::Instance& buffer, bool) {
  ENVOY_LOG(trace, "Writing {} bytes to the downstream connection", buffer.length());
  switch (state_) {
  case PeerMetadataState::WaitingForData: {
    // If we are receiving data for downstream - there is no point in waiting
    // for peer metadata anymore, if the upstream sent it, we'd have it by
    // now. So we can check if the peer metadata is available or not, and if
    // no peer metadata available, we can give up waiting for it.
    std::optional<Envoy::Protobuf::Any> peer_metadata = discoverPeerMetadata();
    if (config_.mode() == MetadataExchangeMode::DATA_STREAM_PREAMBLE) {
      // Legacy: push the peer metadata (or an empty marker) into the data stream.
      if (peer_metadata) {
        propagatePeerMetadata(*peer_metadata);
      } else {
        propagateNoPeerMetadata();
      }
    } else if (peer_metadata) {
      // Default: hand off via the thread-local registry.
      storeInRegistry(*peer_metadata);
    }
    state_ = PeerMetadataState::PassThrough;
    break;
  }
  default:
    break;
  }
  return Network::FilterStatus::Continue;
}

void Filter::initializeWriteFilterCallbacks(Network::WriteFilterCallbacks& callbacks) {
  write_callbacks_ = &callbacks;
}

void Filter::populateBaggage() {
  if (config_.baggage_key().empty()) {
    ENVOY_LOG(trace, "Not populating baggage filter state because baggage key is not set");
    return;
  }

  ENVOY_LOG(trace, "Populating baggage value {} in the filter state with key {}", baggage_,
            config_.baggage_key());
  ASSERT(read_callbacks_);
  read_callbacks_->connection().streamInfo().filterState()->setData(
      config_.baggage_key(), std::make_shared<Router::StringAccessorImpl>(baggage_),
      StreamInfo::FilterState::LifeSpan::FilterChain);
}

bool Filter::disableDiscovery() const {
  ASSERT(read_callbacks_);
  const auto& metadata = read_callbacks_->connection().streamInfo().dynamicMetadata();
  return discoveryDisabled(metadata);
}

// discoveryPeerMetadata is called to check if the baggage HTTP2 CONNECT
// response headers have been populated already in the filter state.
//
// NOTE: It's safe to call this function during any step of processing - it
// will not do anything if the filter is not in the right state.
std::optional<Envoy::Protobuf::Any> Filter::discoverPeerMetadata() {
  ENVOY_LOG(trace, "Trying to discover peer metadata from filter state set by TCP Proxy");
  ASSERT(write_callbacks_);

  const Network::Connection& conn = write_callbacks_->connection();
  const StreamInfo::StreamInfo& stream = conn.streamInfo();
  const TcpProxy::TunnelResponseHeaders* state =
      stream.filterState().getDataReadOnly<TcpProxy::TunnelResponseHeaders>(
          TcpProxy::TunnelResponseHeaders::key());
  if (!state) {
    ENVOY_LOG(trace, "TCP Proxy didn't set expected filter state");
    return std::nullopt;
  }

  const Http::HeaderMap& headers = state->value();
  const auto baggage = headers.get(Headers::get().Baggage);
  if (baggage.empty()) {
    ENVOY_LOG(
        trace,
        "TCP Proxy saved response headers to the filter state, but there is no baggage header");
    return std::nullopt;
  }

  ENVOY_LOG(trace,
            "Successfully discovered peer metadata from the baggage header saved by TCP Proxy");

  std::string identity{};
  const auto upstream = write_callbacks_->connection().streamInfo().upstreamInfo();
  if (upstream) {
    const auto conn = upstream->upstreamSslConnection();
    if (conn) {
      identity = absl::StrJoin(conn->uriSanPeerCertificate(), ",");
      ENVOY_LOG(trace, "Discovered upstream peer identity to be {}", identity);
    }
  }

  std::unique_ptr<::Istio::Common::WorkloadMetadataObject> metadata =
      ::Istio::Common::convertBaggageToWorkloadMetadata(baggage[0]->value().getStringView(),
                                                        identity);

  Envoy::Protobuf::Struct data = ::Istio::Common::convertWorkloadMetadataToStruct(*metadata);
  Envoy::Protobuf::Any wrapped;
  std::ignore = wrapped.PackFrom(data);
  return wrapped;
}

void Filter::storeInRegistry(const Envoy::Protobuf::Any& peer_metadata) {
  if (registry_ == nullptr || read_callbacks_ == nullptr) {
    ENVOY_LOG(debug, "No instance for registry or callbacks");
    return;
  }
  const auto* connection_id =
      read_callbacks_->connection()
          .streamInfo()
          .filterState()
          ->getDataReadOnly<Router::StringAccessor>(
              Filters::Common::PeerMetadataShared::ConnectionIdFilterStateKey);
  if (connection_id == nullptr) {
    ENVOY_LOG(debug, "No upstream connection ID in filter state, cannot store in TLS registry");
    return;
  }
  std::string serialized = peer_metadata.SerializeAsString();
  registry_->setValue(connection_id->asString(), serialized);
}

void Filter::propagatePeerMetadata(const Envoy::Protobuf::Any& peer_metadata) {
  ENVOY_LOG(trace, "Sending peer metadata downstream with the data stream");
  ASSERT(write_callbacks_);

  if (state_ != PeerMetadataState::WaitingForData) {
    // It's only safe and correct to send the peer metadata downstream with
    // the data if we haven't done that already, otherwise the downstream
    // could be very confused by the data they received.
    ENVOY_LOG(trace, "Filter has already sent the peer metadata downstream");
    return;
  }

  std::string data = peer_metadata.SerializeAsString();
  PeerMetadataHeader header{PeerMetadataHeader::magic_number, static_cast<uint32_t>(data.size())};

  Buffer::OwnedImpl buffer{
      absl::string_view(reinterpret_cast<const char*>(&header), sizeof(header))};
  buffer.add(data);
  write_callbacks_->injectWriteDataToFilterChain(buffer, false);
}

void Filter::propagateNoPeerMetadata() {
  ENVOY_LOG(trace, "Sending no peer metadata downstream with the data");
  ASSERT(write_callbacks_);

  PeerMetadataHeader header{PeerMetadataHeader::magic_number, 0};
  Buffer::OwnedImpl buffer{
      absl::string_view(reinterpret_cast<const char*>(&header), sizeof(header))};
  write_callbacks_->injectWriteDataToFilterChain(buffer, false);
}

UpstreamFilter::UpstreamFilter(
    MetadataExchangeMode mode,
    Filters::Common::PeerMetadataShared::PeerMetadataRegistrySharedPtr registry)
    : mode_(mode), registry_(std::move(registry)) {}

Network::FilterStatus UpstreamFilter::onData(Buffer::Instance& buffer, bool end_stream) {
  switch (state_) {
  case PeerMetadataState::WaitingForData:
    if (disableDiscovery()) {
      state_ = PeerMetadataState::PassThrough;
      break;
    }
    if (mode_ == MetadataExchangeMode::DATA_STREAM_PREAMBLE) {
      // Legacy: parse and strip the peer metadata preamble from the data stream.
      if (consumePeerMetadata(buffer, end_stream)) {
        state_ = PeerMetadataState::PassThrough;
      } else {
        // Waiting for more data to complete the preamble.
        return Network::FilterStatus::StopIteration;
      }
    } else if (tryRegistryLookup()) {
      state_ = PeerMetadataState::PassThrough;
    } else {
      ENVOY_LOG(trace, "No peer metadata available in registry");
      populateNoPeerMetadata();
      state_ = PeerMetadataState::PassThrough;
    }
    break;
  default:
    break;
  }

  return Network::FilterStatus::Continue;
}

Network::FilterStatus UpstreamFilter::onNewConnection() { return Network::FilterStatus::Continue; }

void UpstreamFilter::initializeReadFilterCallbacks(Network::ReadFilterCallbacks& callbacks) {
  callbacks_ = &callbacks;
}

// We want to enable baggage based peer metadata discovery if all of the following is true
// - the upstream host is an internal listener, and specifically connect_originate or
//   inner_connect_originate internal listener - those are the only listeners that
//   support baggage-based peer metadata discovery
// - communication with upstream happens in plain text, e.g., there is no TLS upstream
//   transport socket or PROXY transport socket there - we need it in the current
//   implemetation of the baggage-based peer metadata discovery because we inject peer
//   metadata into the data stream and transport sockets that modify the data stream
//   interfere with that (NOTE: in the future release we are planning to lift this
//   limitation by communicating over shared memory instead).
//
// We can easily check if the upstream host is an internal listener, so checking the first
// condition is easy. We can't easily check the second condition in the filter itself, so
// instead we rely on istiod providing that information in form of the host metadata on the
// endpoint or cluster level.
bool UpstreamFilter::disableDiscovery() const {
  ASSERT(callbacks_);

  const auto upstream = callbacks_->connection().streamInfo().upstreamInfo();
  if (!upstream) {
    ENVOY_LOG(error, "No upstream information, cannot confirm that upstream uses HBONE");
    return false;
  }

  const auto host = upstream->upstreamHost();
  if (!host) {
    ENVOY_LOG(error, "No upstream host, cannot confirm that upstream host uses HBONE");
    return false;
  }

  if (!allowedInternalListener(*host->address())) {
    ENVOY_LOG(
        trace,
        "Upstream host is not connect_originate or inner_connect_originate internal listener");
    return true;
  }

  if (discoveryDisabled(*host->metadata()) || discoveryDisabled(host->cluster().metadata())) {
    ENVOY_LOG(trace, "Peer metadata discovery explicitly disabled via metadata");
    return true;
  }

  return false;
}

bool UpstreamFilter::tryRegistryLookup() {
  if (registry_ == nullptr || callbacks_ == nullptr) {
    return false;
  }
  // UpstreamFilter reads the downstream connection ID shared via transitive
  // filter state and use it as the registry key, the listener-side Filter
  // stores peer metadata under the same key.
  const auto* connection_id =
      callbacks_->connection().streamInfo().filterState()->getDataReadOnly<Router::StringAccessor>(
          Filters::Common::PeerMetadataShared::ConnectionIdFilterStateKey);
  if (connection_id == nullptr) {
    ENVOY_LOG(debug, "No downstream connection ID in filter state");
    return false;
  }
  absl::string_view conn_id = connection_id->asString();
  auto value = registry_->getValue(conn_id);
  if (!value.has_value()) {
    ENVOY_LOG(debug, "No peer metadata in registry for connection ID {}", conn_id);
    return false;
  }

  registry_->removeValue(conn_id);

  Envoy::Protobuf::Any any;
  if (!any.ParseFromString(*value)) {
    ENVOY_LOG(error, "Failed to parse peer metadata from registry");
    populateNoPeerMetadata();
    return true;
  }

  Envoy::Protobuf::Struct peer_metadata;
  if (!any.UnpackTo(&peer_metadata)) {
    ENVOY_LOG(error, "Failed to unpack peer metadata struct from registry");
    populateNoPeerMetadata();
    return true;
  }

  std::unique_ptr<::Istio::Common::WorkloadMetadataObject> workload =
      ::Istio::Common::convertStructToWorkloadMetadata(peer_metadata);
  populatePeerMetadata(*workload);
  ENVOY_LOG(trace, "Successfully retrieved peer metadata from registry");
  return true;
}

bool UpstreamFilter::consumePeerMetadata(Buffer::Instance& buffer, bool end_stream) {
  ENVOY_LOG(trace, "Trying to consume peer metadata from the data stream");
  using namespace ::Istio::Common;

  ASSERT(callbacks_);

  if (state_ != PeerMetadataState::WaitingForData) {
    ENVOY_LOG(trace, "The filter already consumed peer metadata from the data stream");
    return true;
  }

  if (buffer.length() < sizeof(PeerMetadataHeader)) {
    if (end_stream) {
      ENVOY_LOG(trace, "Not enough data in the data stream for peer metadata header and no more "
                       "data is coming");
      populateNoPeerMetadata();
      return true;
    }
    ENVOY_LOG(trace,
              "Not enough data in the data stream for peer metadata header, waiting for more data");
    return false;
  }

  PeerMetadataHeader header;
  buffer.copyOut(0, sizeof(PeerMetadataHeader), &header);

  if (header.magic != PeerMetadataHeader::magic_number) {
    ENVOY_LOG(trace, "Magic number in the peer metadata header didn't match expected value");
    populateNoPeerMetadata();
    return true;
  }

  if (header.data_size == 0) {
    ENVOY_LOG(trace, "Peer metadata is empty");
    populateNoPeerMetadata();
    buffer.drain(sizeof(PeerMetadataHeader));
    return true;
  }

  const size_t peer_metadata_size = sizeof(PeerMetadataHeader) + header.data_size;

  if (buffer.length() < peer_metadata_size) {
    if (end_stream) {
      ENVOY_LOG(trace,
                "Not enough data in the data stream for peer metadata and no more data is coming");
      populateNoPeerMetadata();
      return true;
    }
    ENVOY_LOG(trace, "Not enough data in the data stream for peer metadata, waiting for more data");
    return false;
  }

  absl::string_view data{static_cast<const char*>(buffer.linearize(peer_metadata_size)),
                         peer_metadata_size};
  data = data.substr(sizeof(PeerMetadataHeader));
  Envoy::Protobuf::Any any;
  if (!any.ParseFromArray(data.data(), data.size())) {
    ENVOY_LOG(trace, "Failed to parse peer metadata proto from the data stream");
    populateNoPeerMetadata();
    return true;
  }

  Envoy::Protobuf::Struct peer_metadata;
  if (!any.UnpackTo(&peer_metadata)) {
    ENVOY_LOG(trace, "Failed to unpack peer metadata struct");
    populateNoPeerMetadata();
    return true;
  }

  std::unique_ptr<WorkloadMetadataObject> workload = convertStructToWorkloadMetadata(peer_metadata);
  populatePeerMetadata(*workload);
  buffer.drain(peer_metadata_size);
  ENVOY_LOG(trace, "Successfully consumed peer metadata from the data stream");
  return true;
}

const CelStatePrototype& UpstreamFilter::peerInfoPrototype() {
  static const CelStatePrototype* const prototype = new CelStatePrototype(
      true, CelStateType::Protobuf, "type.googleapis.com/google.protobuf.Struct",
      StreamInfo::FilterState::LifeSpan::Connection);
  return *prototype;
}

void UpstreamFilter::populatePeerMetadata(const ::Istio::Common::WorkloadMetadataObject& peer) {
  ENVOY_LOG(trace, "Populating peer metadata in the upstream filter state");
  ASSERT(callbacks_);

  auto proto = ::Istio::Common::convertWorkloadMetadataToStruct(peer);
  auto cel = std::make_shared<CelState>(peerInfoPrototype());
  cel->setValue(absl::string_view(proto.SerializeAsString()));
  callbacks_->connection().streamInfo().filterState()->setData(
      ::Istio::Common::UpstreamPeer, std::move(cel), StreamInfo::FilterState::LifeSpan::Connection);
  // Also store the WorkloadMetadataObject under the *_obj key, mirroring the
  // metadata_exchange network filter. istio.stats reads the object directly
  // (peerInfoRead/peerInfo prefer it); its CelState fallback uses BaggageToken
  // keys that are incompatible with convertWorkloadMetadataToStruct's output, so
  // without this the peer identity is lost over HBONE.
  callbacks_->connection().streamInfo().filterState()->setData(
      ::Istio::Common::UpstreamPeerObj,
      std::make_shared<::Istio::Common::WorkloadMetadataObject>(peer),
      StreamInfo::FilterState::LifeSpan::Connection);
}

void UpstreamFilter::populateNoPeerMetadata() {
  ENVOY_LOG(trace, "Populating no peer metadata in the upstream filter state");
  ASSERT(callbacks_);

  callbacks_->connection().streamInfo().filterState()->setData(
      ::Istio::Common::NoPeer, std::make_shared<StreamInfo::BoolAccessorImpl>(true),
      StreamInfo::FilterState::LifeSpan::Connection);
}

ConfigFactory::ConfigFactory()
    : Common::ExceptionFreeFactoryBase<Config>("envoy.filters.network.peer_metadata",
                                               /*is_terminal*/ false) {}

absl::StatusOr<Network::FilterFactoryCb>
ConfigFactory::createFilterFactoryFromProtoTyped(const Config& config,
                                                 Server::Configuration::FactoryContext& context) {
  // Only the thread-local registry mode allocates an Envoy TLS slot; the legacy
  // data-stream mode needs no registry.
  Filters::Common::PeerMetadataShared::PeerMetadataRegistrySharedPtr registry;
  if (config.mode() == MetadataExchangeMode::THREAD_LOCAL_REGISTRY) {
    registry = Filters::Common::PeerMetadataShared::getRegistry(context.serverFactoryContext());
  }
  return [config, &context,
          registry = std::move(registry)](Network::FilterManager& filter_manager) -> void {
    const auto& local_info = context.serverFactoryContext().localInfo();
    filter_manager.addFilter(std::make_shared<Filter>(config, local_info, registry));
  };
}

Network::FilterFactoryCb UpstreamConfigFactory::createFilterFactoryFromProto(
    const Protobuf::Message& config, Server::Configuration::UpstreamFactoryContext& context) {
  const auto& typed_config = dynamic_cast<const UpstreamConfig&>(config);
  const MetadataExchangeMode mode = typed_config.mode();
  Filters::Common::PeerMetadataShared::PeerMetadataRegistrySharedPtr registry;
  if (mode == MetadataExchangeMode::THREAD_LOCAL_REGISTRY) {
    registry = Filters::Common::PeerMetadataShared::getRegistry(context.serverFactoryContext());
  }
  return [mode, registry = std::move(registry)](Network::FilterManager& filter_manager) -> void {
    filter_manager.addReadFilter(std::make_shared<UpstreamFilter>(mode, registry));
  };
}

ProtobufTypes::MessagePtr UpstreamConfigFactory::createEmptyConfigProto() {
  return std::make_unique<UpstreamConfig>();
}

std::string UpstreamConfigFactory::name() const {
  return "envoy.filters.network.upstream.peer_metadata";
}

bool UpstreamConfigFactory::isTerminalFilterByProto(const Protobuf::Message&,
                                                    Server::Configuration::ServerFactoryContext&) {
  // This filter must be last filter in the upstream filter chain, so that
  // it'd be the first filter to see and process the data coming back,
  // because it has to remove the preamble set by the network filter.
  return true;
}

namespace {

REGISTER_FACTORY(ConfigFactory, Server::Configuration::NamedNetworkFilterConfigFactory);
REGISTER_FACTORY(UpstreamConfigFactory,
                 Server::Configuration::NamedUpstreamNetworkFilterConfigFactory);

} // namespace

} // namespace PeerMetadata
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
