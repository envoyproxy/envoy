#pragma once

#include "envoy/extensions/bootstrap/reverse_tunnel/reverse_tunnel_reporter.h"

#include "contrib/envoy/extensions/reverse_tunnel_reporters/v3alpha/reporters/event_reporter.pb.h"
#include "contrib/envoy/extensions/reverse_tunnel_reporters/v3alpha/reporters/event_reporter.pb.validate.h"
#include "contrib/reverse_tunnel_reporter/source/reporters/event_reporter/reporter.h"
#include "contrib/reverse_tunnel_reporter/source/reverse_tunnel_event_types.h"

namespace Envoy {
namespace Extensions {
namespace Bootstrap {
namespace ReverseConnection {

/// Factory that builds an EventReporter from its proto config, dynamically
/// resolving each child ReverseTunnelReporterClient by name.
class EventReporterFactory : public ReverseTunnelReporterFactory {
public:
  ReverseTunnelReporterPtr createReporter(Server::Configuration::ServerFactoryContext& context,
                                          ProtobufTypes::MessagePtr config) override;
  std::string name() const override;
  ProtobufTypes::MessagePtr createEmptyConfigProto() override;

private:
  using ConfigProto =
      envoy::extensions::reverse_tunnel_reporters::v3alpha::reporters::EventReporterConfig;
  using ClientConfigProto = envoy::extensions::reverse_tunnel_reporters::v3alpha::reporters::
      ReverseConnectionReporterClient;

  ReverseTunnelReporterClientPtr createClient(Server::Configuration::ServerFactoryContext& context,
                                              const ClientConfigProto& client_config);
};

} // namespace ReverseConnection
} // namespace Bootstrap
} // namespace Extensions
} // namespace Envoy
