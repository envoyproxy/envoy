#pragma once

#include <string>

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

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace PeerMetadata {

using Config = ::envoy::extensions::filters::network::peer_metadata::v3::Config;
using UpstreamConfig = ::envoy::extensions::filters::network::peer_metadata::v3::UpstreamConfig;
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
 * the upstream peer principle, encode those details into a sequence of bytes
 * and will inject it dowstream.
 */
class Filter : public Network::Filter, Logger::Loggable<Logger::Id::filter> {
public:
  Filter(const Config& config, const LocalInfo::LocalInfo& local_info);

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
  absl::optional<Protobuf::Any> discoverPeerMetadata();
  void propagatePeerMetadata(const Protobuf::Any& peer_metadata);
  void propagateNoPeerMetadata();

  PeerMetadataState state_ = PeerMetadataState::WaitingForData;
  Network::WriteFilterCallbacks* write_callbacks_{};
  Network::ReadFilterCallbacks* read_callbacks_{};
  Config config_;
  std::string baggage_;
};

/**
 * This is an upstream network filter complementing the filter above. It will
 * be installed in all the service clusters that may use HBONE (or double
 * HBONE) to communicate with the upstream peers and it will parse and remove
 * the data injected by the filter above. The parsed peer metadata details will
 * be saved in the filter state.
 */
class UpstreamFilter : public Network::ReadFilter, Logger::Loggable<Logger::Id::filter> {
public:
  UpstreamFilter();

  // Network::ReadFilter
  Network::FilterStatus onData(Buffer::Instance& buffer, bool end_stream) override;
  Network::FilterStatus onNewConnection() override;
  void initializeReadFilterCallbacks(Network::ReadFilterCallbacks& callbacks) override;

private:
  bool disableDiscovery() const;
  bool consumePeerMetadata(Buffer::Instance& buffer, bool end_stream);

  static const CelStatePrototype& peerInfoPrototype();

  void populatePeerMetadata(const Istio::Common::WorkloadMetadataObject& peer);
  void populateNoPeerMetadata();

  PeerMetadataState state_ = PeerMetadataState::WaitingForData;
  Network::ReadFilterCallbacks* callbacks_{};
};

/**
 * PeerMetadata network filter factory.
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
 */
class UpstreamConfigFactory
    : public Server::Configuration::NamedUpstreamNetworkFilterConfigFactory {
public:
  Network::FilterFactoryCb
  createFilterFactoryFromProto(const Protobuf::Message& config,
                               Server::Configuration::UpstreamFactoryContext&) override;
  ProtobufTypes::MessagePtr createEmptyConfigProto() override;
  std::string name() const override;
  bool isTerminalFilterByProto(const Protobuf::Message&,
                               Server::Configuration::ServerFactoryContext&) override;

private:
  Network::FilterFactoryCb createFilterFactory(const UpstreamConfig&);
};

} // namespace PeerMetadata
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
