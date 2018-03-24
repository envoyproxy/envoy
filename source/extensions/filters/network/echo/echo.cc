#include "extensions/filters/network/echo/echo.h"

#include "envoy/buffer/buffer.h"
#include "envoy/network/connection.h"
#include "envoy/registry/registry.h"
#include "envoy/server/filter_config.h"

#include "common/common/assert.h"
#include "common/config/well_known_names.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilter {
namespace Echo {

Network::FilterStatus Instance::onData(Buffer::Instance& data, bool end_stream) {
  ENVOY_CONN_LOG(trace, "echo: got {} bytes", read_callbacks_->connection(), data.length());
  read_callbacks_->connection().write(data, end_stream);
  ASSERT(0 == data.length());
  return Network::FilterStatus::StopIteration;
}

/**
 * Config registration for the echo filter. @see NamedNetworkFilterConfigFactory.
 */
class EchoConfigFactory : public Server::Configuration::NamedNetworkFilterConfigFactory {
public:
  // NamedNetworkFilterConfigFactory
  Server::Configuration::NetworkFilterFactoryCb
  createFilterFactory(const Json::Object&, Server::Configuration::FactoryContext&) override {
    return [](Network::FilterManager& filter_manager) -> void {
      filter_manager.addReadFilter(std::make_shared<Instance>());
    };
  }

  Server::Configuration::NetworkFilterFactoryCb
  createFilterFactoryFromProto(const Protobuf::Message&,
                               Server::Configuration::FactoryContext&) override {
    return [](Network::FilterManager& filter_manager) -> void {
      filter_manager.addReadFilter(std::make_shared<Instance>());
    };
  }

  ProtobufTypes::MessagePtr createEmptyConfigProto() override {
    return ProtobufTypes::MessagePtr{new Envoy::ProtobufWkt::Empty()};
  }

  std::string name() override { return Config::NetworkFilterNames::get().ECHO; }
};

/**
 * Static registration for the echo filter. @see RegisterFactory.
 */
static Registry::RegisterFactory<EchoConfigFactory,
                                 Server::Configuration::NamedNetworkFilterConfigFactory>
    registered_;

} // namespace Echo
} // namespace NetworkFilter
} // namespace Extensions
} // namespace Envoy
