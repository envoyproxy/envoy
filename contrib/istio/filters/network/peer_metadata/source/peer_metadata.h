#pragma once

#include <memory>
#include <optional>
#include <string>

#include "envoy/common/platform.h"
#include "envoy/local_info/local_info.h"
#include "envoy/network/filter.h"
#include "envoy/server/filter_config.h"

#include "source/common/common/logger.h"
#include "source/common/singleton/const_singleton.h"
#include "source/extensions/filters/common/expr/cel_state.h"
#include "source/extensions/filters/network/common/factory_base.h"

#include "contrib/envoy/extensions/filters/network/peer_metadata/v3/peer_metadata.pb.h"
#include "contrib/envoy/extensions/filters/network/peer_metadata/v3/peer_metadata.pb.validate.h"
#include "contrib/istio/filters/common/source/metadata_object.h"
#include "contrib/istio/filters/common/source/peer_metadata_registry.h"

/**
 * PeerMetadata network and upstream network filters are used in one of ambient
 * peer metadata discovery mechanims. The peer metadata discovery mechanism
 * these filters are part of relies on peer reporting their own metadata in
 * HBONE CONNECT request and response headers.
 *
 * The purpose of these filters is to extract this metadata from the request/
 * response headers and propagate it to the Istio filters reporting telemetry
 * where this metadata will be used as labels.
 *
 * The filters in this folder are specifically concerned with extracting and
 * propagating upstream peer metadata. The working setup includes a combination
 * of several filters that together get the job done.
 *
 * A bit of background, here is a very simplified description of how Istio
 * waypoint processes a request:
 *
 * 1. connect_terminate listener recieves an incoming HBONE connection;
 *    * it uwraps HBONE tunnel and extracts the data passed inside it;
 *    * it passes the data inside the HBONE tunnel to a main_internal listener
 *      that performs the next stage of processing;
 * 2. main_internal listener is responsible for parsing the data as L7 data
 *    (HTTP/gRPC), applying configured L7 policies, picking the endpoint to
 *    route the request to and reports L7 stats
 *    * At this level we are processing the incoming request at L7 level and
 *      have access to things like status of the request and can report
 *      meaningful metrics;
 *    * To report in metrics where the request came from and where it went
 *      after we need to know the details of downstream and upstream peers -
 *      that's what we call peer metadata;
 *    * Once we've done with L7 processing of the request, we pass the request
 *      to the connect_originate (or inner_connect_originate in case of double
 *      HBONE) listener that will handle the next stage of processing;
 * 3. connect_originate - is responsible for wrapping processed L7 traffic into
 *    an HBONE tunnel and sending it out
 *    * This stage of processing treats data as a stream of bytes without any
 *      knowledge of L7 protocol details;
 *    * It takes the upstream peer address as input an establishes an HBONE
 *      tunnel to the destination and sends the data via that tunnel.
 *
 * With that picture in mind, what we want to do is in connect_originate (or
 * inner_connect_originate in case of double-HBONE) when we establish HBONE
 * tunnel, we want to extract peer metadata from the CONNECT response and
 * propagate it to the main_internal.
 *
 * To establish HBONE tunnel we rely on Envoy TCP Proxy filter, so we don't
 * handle HTTP2 CONNECT responses or requests directly, instead we rely on the
 * TCP Proxy filter to extract required information from the response and save
 * it in the filter state. We then use the custom network filter to take filter
 * state proved by TCP Proxy filter, encode it, and send it to main_internal
 * *as data* before any actual response data. This is what the network filter
 * defined here is responsible for.
 *
 * In main_internal we use a custom upstream network filter to extract and
 * remove the metadata from the data stream and populate filter state that
 * could be used by Istio telemetry filters. That's what the upstream network
 * filter defined here is responsible for.
 *
 * Why do we do it this way? Generally in Envoy we use filter state and dynamic
 * metadata to communicate additional information between filters. While it's
 * possible to propagate filter state from downstream to upstream, i.e., we
 * could set filter state in connect_terminate and propagate it to
 * main_internal and then to connect_originate, it's not possible to propagate
 * filter state from upstream to downstream, i.e., we cannot make filter state
 * set in connect_originate available to main_internal directly. That's why we
 * push that metadata with the data instead.
 */

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace PeerMetadata {

using Config = ::envoy::extensions::network_filters::peer_metadata::Config;
using UpstreamConfig = ::envoy::extensions::network_filters::peer_metadata::UpstreamConfig;
using CelStatePrototype = ::Envoy::Extensions::Filters::Common::Expr::CelStatePrototype;

struct HeaderValues {
  const Http::LowerCaseString Baggage{"baggage"};
};

using Headers = ConstSingleton<HeaderValues>;

struct FilterNameValues {
  const std::string Name = "istio.peer_metadata";
  const std::string DisableDiscoveryField = "disable_baggage_discovery";
};

using FilterNames = ConstSingleton<FilterNameValues>;

enum class PeerMetadataState {
  WaitingForData,
  PassThrough,
};

// Wire header for the legacy data-stream exchange: a magic number followed by
// the serialized peer metadata size, injected ahead of the real data by the
// downstream filter and stripped by the upstream filter.
PACKED_STRUCT(struct PeerMetadataHeader {
  uint32_t magic;
  static const uint32_t magic_number;
  uint32_t data_size;
});

/**
 * This is a regular network filter that will be installed in the
 * connect_originate or inner_connect_originate filter chains. It will take
 * baggage header information from filter state (we expect TCP Proxy to
 * populate it), collect other details that are missing from the baggage, i.e.
 * the upstream peer identity, and store the discovered peer metadata in a
 * thread-local registry keyed by the upstream connection ID, allowing the
 * upstream peer_metadata filter on the cluster side to retrieve it directly.
 */
class Filter : public Network::Filter, Logger::Loggable<Logger::Id::filter> {
public:
  Filter(const Config& config, const LocalInfo::LocalInfo& local_info,
         Filters::Common::PeerMetadataShared::PeerMetadataRegistrySharedPtr registry);

  // Network::ReadFilter
  Network::FilterStatus onNewConnection() override;
  Network::FilterStatus onData(Buffer::Instance&, bool) override;
  void initializeReadFilterCallbacks(Network::ReadFilterCallbacks& callbacks) override;

  // Network::WriteFilter
  Network::FilterStatus onWrite(Buffer::Instance& buffer, bool) override;
  void initializeWriteFilterCallbacks(Network::WriteFilterCallbacks& callbacks) override;

private:
  void populateBaggage();
  bool disableDiscovery() const;
  std::optional<Envoy::Protobuf::Any> discoverPeerMetadata();
  // Store discovered peer metadata in the thread-local registry, keyed by the
  // downstream connection ID from filter state. Returns false when there is no
  // connection ID in filter state (registry hand-off unavailable), so the caller
  // can fall back to the data-stream approach.
  bool storeInRegistry(const std::optional<Envoy::Protobuf::Any>& peer_metadata);
  // Legacy fallback: inject the peer metadata (or an empty marker) as a preamble
  // into the downstream data stream.
  void propagatePeerMetadata(const Envoy::Protobuf::Any& peer_metadata);
  void propagateNoPeerMetadata();

  PeerMetadataState state_ = PeerMetadataState::WaitingForData;
  Network::WriteFilterCallbacks* write_callbacks_{};
  Network::ReadFilterCallbacks* read_callbacks_{};
  Config config_;
  std::string baggage_;
  Filters::Common::PeerMetadataShared::PeerMetadataRegistrySharedPtr registry_;
};

/**
 * This is an upstream network filter complementing the filter above. It will
 * be installed in all the service clusters that communicate with upstream peers
 * via internal listeners. It reads peer metadata from the thread-local registry
 * (stored by the listener-side Filter keyed by this connection's ID) and
 * populates filter state for use by telemetry filters.
 */
class UpstreamFilter : public Network::ReadFilter, Logger::Loggable<Logger::Id::filter> {
public:
  UpstreamFilter(Filters::Common::PeerMetadataShared::PeerMetadataRegistrySharedPtr registry);

  // Network::ReadFilter
  Network::FilterStatus onData(Buffer::Instance& buffer, bool end_stream) override;
  Network::FilterStatus onNewConnection() override;
  void initializeReadFilterCallbacks(Network::ReadFilterCallbacks& callbacks) override;

private:
  bool disableDiscovery() const;
  // Look up peer metadata stored by the paired downstream filter, keyed by this
  // connection's ID from filter state. Returns false when there is no connection
  // ID in filter state or the TLS registry is not allocated.
  bool tryRegistryLookup();
  // Legacy fallback: parse and strip the peer metadata preamble from the
  // upstream data stream.
  bool consumePeerMetadata(Buffer::Instance& buffer, bool end_stream);

  static const CelStatePrototype& peerInfoPrototype();

  // Parse serialized peer metadata (a serialized google.protobuf.Any wrapping a
  // Struct), convert it to a WorkloadMetadataObject and populate filter state.
  // On any parsing/unpacking failure, populates the no-peer-metadata marker
  // instead.
  void populatePeerMetadataFromProto(absl::string_view serialized);
  void populatePeerMetadata(const ::Istio::Common::WorkloadMetadataObject& peer);
  void populateNoPeerMetadata();

  PeerMetadataState state_ = PeerMetadataState::WaitingForData;
  Network::ReadFilterCallbacks* callbacks_{};
  Filters::Common::PeerMetadataShared::PeerMetadataRegistrySharedPtr registry_;
};

/**
 * PeerMetadata network filter factory.
 *
 * This filter is responsible for collecting peer metadata from filter state
 * and other sources, encoding it and passing it downstream before the actual
 * data.
 */
class ConfigFactory : public Common::ExceptionFreeFactoryBase<Config> {
public:
  ConfigFactory();

private:
  absl::StatusOr<Network::FilterFactoryCb>
  createFilterFactoryFromProtoTyped(const Config& config,
                                    Server::Configuration::FactoryContext& context) override;
};

/**
 * PeerMetadata upstream network filter factory.
 *
 * This filter is responsible for detecting the peer metadata passed in the
 * data stream, parsing it, populating filter state based on that and finally
 * removing it from the data stream, so that downstream filters can process
 * the data as usual.
 */
class UpstreamConfigFactory
    : public Server::Configuration::NamedUpstreamNetworkFilterConfigFactory {
public:
  Network::FilterFactoryCb
  createFilterFactoryFromProto(const Protobuf::Message& config,
                               Server::Configuration::UpstreamFactoryContext& context) override;
  ProtobufTypes::MessagePtr createEmptyConfigProto() override;
  std::string name() const override;
  bool isTerminalFilterByProto(const Protobuf::Message&,
                               Server::Configuration::ServerFactoryContext&) override;
};

} // namespace PeerMetadata
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
