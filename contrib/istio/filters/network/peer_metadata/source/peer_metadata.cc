#include "contrib/istio/filters/network/peer_metadata/source/peer_metadata.h"

#include "source/common/router/string_accessor_impl.h"
#include "source/common/stream_info/bool_accessor_impl.h"
#include "source/common/tcp_proxy/tcp_proxy.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace PeerMetadata {

namespace {

using CelState = ::Envoy::Extensions::Filters::Common::Expr::CelState;
using CelStateType = ::Envoy::Extensions::Filters::Common::Expr::CelStateType;

std::string baggageValue(const LocalInfo::LocalInfo& local_info) {
  const auto obj = Istio::Common::convertStructToWorkloadMetadata(local_info.node().metadata());
  return obj->baggage();
}

absl::optional<absl::string_view> internalListenerName(const Network::Address::Instance& address) {
  const auto internal = address.envoyInternalAddress();
  if (internal != nullptr) {
    return internal->addressId();
  }
  return absl::nullopt;
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

Filter::Filter(const Config& config, const LocalInfo::LocalInfo& local_info)
    : config_(config), baggage_(baggageValue(local_info)) {}

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
    absl::optional<Protobuf::Any> peer_metadata = discoverPeerMetadata();
    if (peer_metadata) {
      propagatePeerMetadata(*peer_metadata);
    } else {
      propagateNoPeerMetadata();
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
      StreamInfo::FilterState::StateType::ReadOnly, StreamInfo::FilterState::LifeSpan::FilterChain);
}

bool Filter::disableDiscovery() const {
  ASSERT(read_callbacks_);
  const auto& metadata = read_callbacks_->connection().streamInfo().dynamicMetadata();
  return discoveryDisabled(metadata);
}

absl::optional<Protobuf::Any> Filter::discoverPeerMetadata() {
  ENVOY_LOG(trace, "Trying to discover peer metadata from filter state set by TCP Proxy");
  ASSERT(write_callbacks_);

  const Network::Connection& conn = write_callbacks_->connection();
  const StreamInfo::StreamInfo& stream = conn.streamInfo();
  const TcpProxy::TunnelResponseHeaders* state =
      stream.filterState().getDataReadOnly<TcpProxy::TunnelResponseHeaders>(
          TcpProxy::TunnelResponseHeaders::key());
  if (!state) {
    ENVOY_LOG(trace, "TCP Proxy didn't set expected filter state");
    return absl::nullopt;
  }

  const Http::HeaderMap& headers = state->value();
  const auto baggage = headers.get(Headers::get().Baggage);
  if (baggage.empty()) {
    ENVOY_LOG(
        trace,
        "TCP Proxy saved response headers to the filter state, but there is no baggage header");
    return absl::nullopt;
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

  std::unique_ptr<Istio::Common::WorkloadMetadataObject> metadata =
      Istio::Common::convertBaggageToWorkloadMetadata(baggage[0]->value().getStringView(),
                                                      identity);

  Protobuf::Struct data = Istio::Common::convertWorkloadMetadataToStruct(*metadata);
  Protobuf::Any wrapped;
  wrapped.PackFrom(data);
  return wrapped;
}

void Filter::propagatePeerMetadata(const Protobuf::Any& peer_metadata) {
  ENVOY_LOG(trace, "Sending peer metadata downstream with the data stream");
  ASSERT(write_callbacks_);

  if (state_ != PeerMetadataState::WaitingForData) {
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

UpstreamFilter::UpstreamFilter() = default;

Network::FilterStatus UpstreamFilter::onData(Buffer::Instance& buffer, bool end_stream) {
  ENVOY_LOG(trace, "Read {} bytes from the upstream connection", buffer.length());

  switch (state_) {
  case PeerMetadataState::WaitingForData:
    if (disableDiscovery()) {
      state_ = PeerMetadataState::PassThrough;
      break;
    }
    if (consumePeerMetadata(buffer, end_stream)) {
      state_ = PeerMetadataState::PassThrough;
    } else {
      return Network::FilterStatus::StopIteration;
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

bool UpstreamFilter::consumePeerMetadata(Buffer::Instance& buffer, bool end_stream) {
  ENVOY_LOG(trace, "Trying to consume peer metadata from the data stream");
  using namespace Istio::Common;

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
  Protobuf::Any any;
  if (!any.ParseFromArray(data.data(), data.size())) {
    ENVOY_LOG(trace, "Failed to parse peer metadata proto from the data stream");
    populateNoPeerMetadata();
    return true;
  }

  Protobuf::Struct peer_metadata;
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

void UpstreamFilter::populatePeerMetadata(const Istio::Common::WorkloadMetadataObject& peer) {
  ENVOY_LOG(trace, "Populating peer metadata in the upstream filter state");
  ASSERT(callbacks_);

  auto proto = Istio::Common::convertWorkloadMetadataToStruct(peer);
  auto cel = std::make_shared<CelState>(peerInfoPrototype());
  cel->setValue(absl::string_view(proto.SerializeAsString()));
  callbacks_->connection().streamInfo().filterState()->setData(
      Istio::Common::UpstreamPeer, std::move(cel), StreamInfo::FilterState::StateType::ReadOnly,
      StreamInfo::FilterState::LifeSpan::Connection);
}

void UpstreamFilter::populateNoPeerMetadata() {
  ENVOY_LOG(trace, "Populating no peer metadata in the upstream filter state");
  ASSERT(callbacks_);

  callbacks_->connection().streamInfo().filterState()->setData(
      Istio::Common::NoPeer, std::make_shared<StreamInfo::BoolAccessorImpl>(true),
      StreamInfo::FilterState::StateType::ReadOnly, StreamInfo::FilterState::LifeSpan::Connection);
}

ConfigFactory::ConfigFactory()
    : Common::ExceptionFreeFactoryBase<Config>("envoy.filters.network.peer_metadata",
                                               /*is_terminal*/ false) {}

absl::StatusOr<Network::FilterFactoryCb>
ConfigFactory::createFilterFactoryFromProtoTyped(const Config& config,
                                                 Server::Configuration::FactoryContext& context) {
  return [config, &context](Network::FilterManager& filter_manager) -> void {
    const auto& local_info = context.serverFactoryContext().localInfo();
    filter_manager.addFilter(std::make_shared<Filter>(config, local_info));
  };
}

Network::FilterFactoryCb UpstreamConfigFactory::createFilterFactoryFromProto(
    const Protobuf::Message& config, Server::Configuration::UpstreamFactoryContext&) {
  return createFilterFactory(dynamic_cast<const UpstreamConfig&>(config));
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

Network::FilterFactoryCb UpstreamConfigFactory::createFilterFactory(const UpstreamConfig&) {
  return [](Network::FilterManager& filter_manager) -> void {
    filter_manager.addReadFilter(std::make_shared<UpstreamFilter>());
  };
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
