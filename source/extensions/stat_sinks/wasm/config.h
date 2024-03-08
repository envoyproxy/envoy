#pragma once

#include <string>

#include "envoy/server/factory_context.h"
#include "envoy/server/instance.h"

#include "source/common/config/datasource.h"
#include "source/extensions/common/wasm/remote_async_datasource.h"
#include "source/server/configuration_impl.h"

namespace Envoy {
namespace Extensions {
namespace StatSinks {
namespace Wasm {

// WebAssembly sink
constexpr char WasmName[] = "envoy.stat_sinks.wasm";

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
  RemoteAsyncDataProviderPtr remote_data_provider_;
};

} // namespace Wasm
} // namespace StatSinks
} // namespace Extensions
} // namespace Envoy
