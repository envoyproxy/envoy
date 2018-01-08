#pragma once

#include <string>

#include "envoy/server/access_log_config.h"

namespace Envoy {
namespace Server {
namespace Configuration {

/**
 * Config registration for the HTTP gRPC access log. @see AccessLogInstanceFactory.
 */
class HttpGrpcAccessLogFactory : public AccessLogInstanceFactory {
public:
  AccessLog::InstanceSharedPtr createAccessLogInstance(const Protobuf::Message& config,
                                                       AccessLog::FilterPtr&& filter,
                                                       FactoryContext& context) override;

  ProtobufTypes::MessagePtr createEmptyConfigProto() override;

  std::string name() const override;
};

// TODO(mattklein123): Add TCP access log and refactor into base/concrete gRPC access logs.

} // namespace Configuration
} // namespace Server
} // namespace Envoy
