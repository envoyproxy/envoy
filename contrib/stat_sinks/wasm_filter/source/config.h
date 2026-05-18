#pragma once

#include <string>

#include "envoy/server/factory_context.h"

#include "source/extensions/common/wasm/remote_async_datasource.h"
#include "source/server/configuration_impl.h"

namespace Envoy {
namespace Extensions {
namespace StatSinks {
namespace WasmFilter {

constexpr char WasmFilterName[] = "envoy.stat_sinks.wasm_filter";

class WasmFilterSinkFactory : Logger::Loggable<Logger::Id::config>,
                              public Server::Configuration::StatsSinkFactory {
public:
  absl::StatusOr<Stats::SinkPtr>
  createStatsSink(const Protobuf::Message& config,
                  Server::Configuration::ServerFactoryContext& context) override;

  ProtobufTypes::MessagePtr createEmptyConfigProto() override;

  std::string name() const override;

private:
  RemoteAsyncDataProviderPtr remote_data_provider_;
};

} // namespace WasmFilter
} // namespace StatSinks
} // namespace Extensions
} // namespace Envoy
