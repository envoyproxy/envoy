#include "contrib/reverse_tunnel_reporter/source/reporters/event_reporter/factory.h"

#include "envoy/registry/registry.h"

#include "source/common/config/utility.h"
#include "source/common/protobuf/utility.h"

namespace Envoy {
namespace Extensions {
namespace Bootstrap {
namespace ReverseConnection {

ReverseTunnelReporterPtr
EventReporterFactory::createReporter(Server::Configuration::ServerFactoryContext& context,
                                     ProtobufTypes::MessagePtr config) {
  const auto& reporter_config = MessageUtil::downcastAndValidate<const ConfigProto&>(
      *config, context.messageValidationVisitor());

  std::vector<ReverseTunnelReporterClientPtr> clients;
  clients.reserve(reporter_config.clients().size());
  for (const auto& client_config : reporter_config.clients()) {
    clients.push_back(createClient(context, client_config));
  }
  return std::make_unique<EventReporter>(context, reporter_config, std::move(clients));
}

std::string EventReporterFactory::name() const {
  return "envoy.extensions.reverse_tunnel.reverse_tunnel_reporting_service.reporters.event_"
         "reporter";
}

ProtobufTypes::MessagePtr EventReporterFactory::createEmptyConfigProto() {
  return std::make_unique<ConfigProto>();
}

ReverseTunnelReporterClientPtr
EventReporterFactory::createClient(Server::Configuration::ServerFactoryContext& context,
                                   const ClientConfigProto& client_config) {
  auto* factory =
      Config::Utility::getFactoryByName<ReverseTunnelReporterClientFactory>(client_config.name());
  if (!factory) {
    throw EnvoyException(
        fmt::format("Unknown Reporter Client Factory: '{}'. "
                    "Make sure it is registered as a ReverseTunnelReporterClientFactory.",
                    client_config.name()));
  }

  auto typed_config = Config::Utility::translateAnyToFactoryConfig(
      client_config.typed_config(), context.messageValidationVisitor(), *factory);
  return factory->createClient(context, *typed_config);
}

REGISTER_FACTORY(EventReporterFactory, ReverseTunnelReporterFactory);

} // namespace ReverseConnection
} // namespace Bootstrap
} // namespace Extensions
} // namespace Envoy
