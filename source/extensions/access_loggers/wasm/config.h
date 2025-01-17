#pragma once

#include "envoy/access_log/access_log_config.h"

#include "source/extensions/common/wasm/remote_async_datasource.h"

namespace Envoy {
namespace Extensions {
namespace AccessLoggers {
namespace Wasm {

/**
 * Config registration for the file access log. @see AccessLogInstanceFactory.
 */
class WasmAccessLogFactory : public AccessLog::AccessLogInstanceFactory,
                             Logger::Loggable<Logger::Id::wasm> {
public:
  AccessLog::InstanceSharedPtr
  createAccessLogInstance(const Protobuf::Message& config, AccessLog::FilterPtr&& filter,
                          Server::Configuration::FactoryContext& context) override;

  ProtobufTypes::MessagePtr createEmptyConfigProto() override;

  std::string name() const override;

private:
  absl::flat_hash_map<std::string, std::string> convertJsonFormatToMap(ProtobufWkt::Struct config);
};

} // namespace Wasm
} // namespace AccessLoggers
} // namespace Extensions
} // namespace Envoy
