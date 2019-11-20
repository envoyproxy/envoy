#pragma once

#include "envoy/server/resource_monitor_config.h"

namespace Envoy {
namespace Server {
namespace Configuration {

class ResourceMonitorFactoryContextImpl : public ResourceMonitorFactoryContext {
public:
  ResourceMonitorFactoryContextImpl(Event::Dispatcher& dispatcher, Api::Api& api,
                                    ProtobufMessage::ValidationVisitor& validation_visitor)
      : dispatcher_(dispatcher), api_(api), validation_visitor_(validation_visitor) {}

  Event::Dispatcher& dispatcher() override { return dispatcher_; }

  Api::Api& api() override { return api_; }

  ProtobufMessage::ValidationVisitor& messageValidationVisitor() override {
    return validation_visitor_;
  }

private:
  Event::Dispatcher& dispatcher_;
  Api::Api& api_;
  ProtobufMessage::ValidationVisitor& validation_visitor_;
};

} // namespace Configuration
} // namespace Server
} // namespace Envoy
