#pragma once

#include "envoy/server/resource_monitor_config.h"

namespace Envoy {
namespace Server {
namespace Configuration {

class ResourceMonitorFactoryContextImpl : public ResourceMonitorFactoryContext {
public:
  ResourceMonitorFactoryContextImpl(Event::Dispatcher& dispatcher, const Server::Options& options,
                                    Api::Api& api,
                                    ProtobufMessage::ValidationVisitor& validation_visitor)
      : dispatcher_(dispatcher), options_(options), api_(api),
        validation_visitor_(validation_visitor) {}

  Event::Dispatcher& mainThreadDispatcher() override { return dispatcher_; }

  const Server::Options& options() override { return options_; }

  Api::Api& api() override { return api_; }

  ProtobufMessage::ValidationVisitor& messageValidationVisitor() override {
    return validation_visitor_;
  }

private:
  Event::Dispatcher& dispatcher_;
  const Server::Options& options_;
  Api::Api& api_;
  ProtobufMessage::ValidationVisitor& validation_visitor_;
};

} // namespace Configuration
} // namespace Server
} // namespace Envoy
