#pragma once

#include "envoy/server/filter_config.h"

#include "contrib/envoy/extensions/filters/network/metadata_exchange/v3/metadata_exchange.pb.h"
#include "contrib/envoy/extensions/filters/network/metadata_exchange/v3/metadata_exchange.pb.validate.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace MetadataExchange {

/**
 * Config registration for the MetadataExchange filter. @see
 *  NamedNetworkFilterConfigFactory.
 */
class MetadataExchangeConfigFactory
    : public Server::Configuration::NamedNetworkFilterConfigFactory {
public:
  absl::StatusOr<Network::FilterFactoryCb>
  createFilterFactoryFromProto(const Protobuf::Message&,
                               Server::Configuration::FactoryContext&) override;

  ProtobufTypes::MessagePtr createEmptyConfigProto() override;

  std::string name() const override { return "envoy.filters.network.metadata_exchange"; }

private:
  Network::FilterFactoryCb
  createFilterFactory(const envoy::tcp::metadataexchange::config::MetadataExchange& proto_config,
                      Server::Configuration::FactoryContext& context);
};

/**
 * Config registration for the MetadataExchange Upstream filter. @see
 *  NamedUpstreamNetworkFilterConfigFactory.
 */
class MetadataExchangeUpstreamConfigFactory
    : public Server::Configuration::NamedUpstreamNetworkFilterConfigFactory {
public:
  Network::FilterFactoryCb
  createFilterFactoryFromProto(const Protobuf::Message&,
                               Server::Configuration::UpstreamFactoryContext&) override;

  ProtobufTypes::MessagePtr createEmptyConfigProto() override;

  std::string name() const override { return "envoy.filters.network.upstream.metadata_exchange"; }

private:
  Network::FilterFactoryCb
  createFilterFactory(const envoy::tcp::metadataexchange::config::MetadataExchange& proto_config,
                      Server::Configuration::UpstreamFactoryContext& context);
};

} // namespace MetadataExchange
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
