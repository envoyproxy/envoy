#pragma once

#include "envoy/config/listener/v3/listener_components.pb.h"
#include "envoy/server/drain_manager.h"
#include "envoy/server/listener_manager.h"

#include "test/mocks/network/mocks.h"

#include "gmock/gmock.h"

namespace Envoy {
namespace Server {
class MockListenerComponentFactory : public ListenerComponentFactory {
public:
  MockListenerComponentFactory();
  ~MockListenerComponentFactory() override;

  DrainManagerPtr
  createDrainManager(envoy::config::listener::v3::Listener::DrainType drain_type) override {
    return DrainManagerPtr{createDrainManager_(drain_type)};
  }
  LdsApiPtr createLdsApi(const envoy::config::core::v3::ConfigSource& lds_config) override {
    return LdsApiPtr{createLdsApi_(lds_config)};
  }

  MOCK_METHOD(LdsApi*, createLdsApi_, (const envoy::config::core::v3::ConfigSource& lds_config));
  MOCK_METHOD(std::vector<Network::FilterFactoryCb>, createNetworkFilterFactoryList,
              (const Protobuf::RepeatedPtrField<envoy::config::listener::v3::Filter>& filters,
               Configuration::FilterChainFactoryContext& filter_chain_factory_context));
  MOCK_METHOD(std::vector<Network::ListenerFilterFactoryCb>, createListenerFilterFactoryList,
              (const Protobuf::RepeatedPtrField<envoy::config::listener::v3::ListenerFilter>&,
               Configuration::ListenerFactoryContext& context));
  MOCK_METHOD(std::vector<Network::UdpListenerFilterFactoryCb>, createUdpListenerFilterFactoryList,
              (const Protobuf::RepeatedPtrField<envoy::config::listener::v3::ListenerFilter>&,
               Configuration::ListenerFactoryContext& context));
  MOCK_METHOD(Network::SocketSharedPtr, createListenSocket,
              (Network::Address::InstanceConstSharedPtr address, Network::Socket::Type socket_type,
               const Network::Socket::OptionsSharedPtr& options,
               const ListenSocketCreationParams& params));
  MOCK_METHOD(DrainManager*, createDrainManager_,
              (envoy::config::listener::v3::Listener::DrainType drain_type));
  MOCK_METHOD(uint64_t, nextListenerTag, ());

  std::shared_ptr<Network::MockListenSocket> socket_;
};
} // namespace Server
} // namespace Envoy
