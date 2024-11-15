#pragma once

#include "source/common/config/metadata.h"

#include "test/mocks/server/factory_context.h"

namespace Envoy {
namespace Server {
namespace Configuration {

class FakeListenerInfo : public Network::ListenerInfo {
public:
  const envoy::config::core::v3::Metadata& metadata() const override {
    return metadata_.proto_metadata_;
  }
  const Envoy::Config::TypedMetadata& typedMetadata() const override {
    return metadata_.typed_metadata_;
  }
  envoy::config::core::v3::TrafficDirection direction() const override {
    return envoy::config::core::v3::UNSPECIFIED;
  }
  bool isQuic() const override { return false; }
  bool shouldBypassOverloadManager() const override { return false; }

private:
  Envoy::Config::MetadataPack<Envoy::Network::ListenerTypedMetadataFactory> metadata_;
};

class FakeFactoryContext : public MockFactoryContext {
public:
  const Network::DrainDecision& drainDecision() override { return drain_manager_; }
  Init::Manager& initManager() override { return init_manager_; }
  Stats::Scope& scope() override { return scope_; }
  Stats::Scope& listenerScope() override { return listener_scope_; }
  ProtobufMessage::ValidationVisitor& messageValidationVisitor() const override {
    return ProtobufMessage::getStrictValidationVisitor();
  }
  const Network::ListenerInfo& listenerInfo() const override { return listener_info_; }

  void prepareSimulatedSystemTime() {
    api_ = Api::createApiForTest(time_system_);
    dispatcher_ = api_->allocateDispatcher("test_thread");

    ON_CALL(server_factory_context_, timeSource()).WillByDefault(testing::ReturnRef(time_system_));
    ON_CALL(server_factory_context_, api()).WillByDefault(testing::ReturnRef(*api_));
    ON_CALL(server_factory_context_, mainThreadDispatcher())
        .WillByDefault(testing::ReturnRef(*dispatcher_));
  }
  Event::SimulatedTimeSystem& simulatedTimeSystem() {
    return dynamic_cast<Event::SimulatedTimeSystem&>(time_system_);
  }

  FakeListenerInfo listener_info_;

  Event::DispatcherPtr dispatcher_;
  Event::SimulatedTimeSystem time_system_;
  Api::ApiPtr api_;
};

} // namespace Configuration
} // namespace Server
} // namespace Envoy
