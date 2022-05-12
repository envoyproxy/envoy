#pragma once

#include "envoy/config/listener/v3/listener_components.pb.h"
#include "envoy/server/drain_manager.h"
#include "envoy/server/listener_manager.h"

#include "source/server/listener_manager_impl.h"

#include "test/mocks/network/mocks.h"
#include "test/mocks/server/instance.h"

#include "gmock/gmock.h"

namespace Envoy {
namespace Server {

class MockProdListenerComponentFactory : public ProdListenerComponentFactory {
public:
  MockProdListenerComponentFactory(NiceMock<Server::MockInstance>& server);
  ~MockProdListenerComponentFactory();

  DrainManagerPtr createDrainManager(envoy::config::listener::v3::Listener::DrainType drain_type) {
    return DrainManagerPtr{createDrainManager_(drain_type)};
  }
  LdsApiPtr createLdsApi(const envoy::config::core::v3::ConfigSource& lds_config,
                         const xds::core::v3::ResourceLocator* lds_resources_locator) {
    return LdsApiPtr{createLdsApi_(lds_config, lds_resources_locator)};
  }
  MOCK_METHOD(LdsApi*, createLdsApi_,
              (const envoy::config::core::v3::ConfigSource&,
               const xds::core::v3::ResourceLocator*));
  MOCK_METHOD(std::vector<Network::FilterFactoryCb>, createNetworkFilterFactoryList,
              (const Protobuf::RepeatedPtrField<envoy::config::listener::v3::Filter>& filters,
               Configuration::FilterChainFactoryContext& filter_chain_factory_context));
  MOCK_METHOD(Filter::ListenerFilterFactoriesList, createListenerFilterFactoryList,
              (const Protobuf::RepeatedPtrField<envoy::config::listener::v3::ListenerFilter>&,
               Configuration::ListenerFactoryContext& context));
  MOCK_METHOD(std::vector<Network::UdpListenerFilterFactoryCb>, createUdpListenerFilterFactoryList,
              (const Protobuf::RepeatedPtrField<envoy::config::listener::v3::ListenerFilter>&,
               Configuration::ListenerFactoryContext& context));
  MOCK_METHOD(Network::SocketSharedPtr, createListenSocket,
              (Network::Address::InstanceConstSharedPtr address, Network::Socket::Type socket_type,
               const Network::Socket::OptionsSharedPtr& options,
               ListenerComponentFactory::BindType bind_type,
               const Network::SocketCreationOptions& creation_options, uint32_t worker_index));
  MOCK_METHOD(DrainManager*, createDrainManager_,
              (envoy::config::listener::v3::Listener::DrainType drain_type));
  MOCK_METHOD(uint64_t, nextListenerTag, ());

  std::shared_ptr<Network::MockListenSocket> socket_;
};

} // namespace Server
} // namespace Envoy
