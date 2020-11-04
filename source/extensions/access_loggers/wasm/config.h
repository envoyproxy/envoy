#pragma once

#include "envoy/server/access_log_config.h"

#include "common/config/datasource.h"

namespace Envoy {
namespace Extensions {
namespace AccessLoggers {
namespace Wasm {

/**
 * Config registration for the file access log. @see AccessLogInstanceFactory.
 */
class WasmAccessLogFactory : public Server::Configuration::AccessLogInstanceFactory,
                             Logger::Loggable<Logger::Id::wasm> {
public:
  AccessLog::InstanceSharedPtr
  createAccessLogInstance(const Protobuf::Message& config, AccessLog::FilterPtr&& filter,
                          Server::Configuration::FactoryContext& context) override;

  ProtobufTypes::MessagePtr createEmptyConfigProto() override;

  std::string name() const override;

private:
  absl::flat_hash_map<std::string, std::string> convertJsonFormatToMap(ProtobufWkt::Struct config);
  Config::DataSource::RemoteAsyncDataProviderPtr remote_data_provider_;
};

} // namespace Wasm
} // namespace AccessLoggers
} // namespace Extensions
} // namespace Envoy
