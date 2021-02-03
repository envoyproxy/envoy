#pragma once

#include <string>

#include "envoy/server/factory_context.h"
#include "envoy/server/instance.h"

#include "common/config/datasource.h"

#include "server/configuration_impl.h"

namespace Envoy {
namespace Extensions {
namespace StatSinks {
namespace Wasm {

/**
 * Config registration for the Wasm statsd sink. @see StatSinkFactory.
 */
class WasmSinkFactory : Logger::Loggable<Logger::Id::config>,
                        public Server::Configuration::StatsSinkFactory {
public:
  // StatsSinkFactory
  Stats::SinkPtr createStatsSink(const Protobuf::Message& config,
                                 Server::Configuration::ServerFactoryContext& context) override;

  ProtobufTypes::MessagePtr createEmptyConfigProto() override;

  std::string name() const override;

private:
  Config::DataSource::RemoteAsyncDataProviderPtr remote_data_provider_;
};

} // namespace Wasm
} // namespace StatSinks
} // namespace Extensions
} // namespace Envoy
