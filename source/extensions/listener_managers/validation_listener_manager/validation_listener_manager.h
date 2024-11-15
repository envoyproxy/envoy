#pragma once

#include "source/common/listener_manager/listener_manager_impl.h"
#include "source/server/listener_manager_factory.h"

namespace Envoy {
namespace Server {

class ValidationListenerComponentFactory : public ListenerComponentFactory {
public:
  ValidationListenerComponentFactory(Instance& parent) : parent_(parent) {}

  // Server::ListenerComponentFactory
  LdsApiPtr createLdsApi(const envoy::config::core::v3::ConfigSource& lds_config,
                         const xds::core::v3::ResourceLocator* lds_resources_locator) override {
    return std::make_unique<LdsApiImpl>(
        lds_config, lds_resources_locator, parent_.clusterManager(), parent_.initManager(),
        *parent_.stats().rootScope(), parent_.listenerManager(),
        parent_.messageValidationContext().dynamicValidationVisitor());
  }
  absl::StatusOr<Filter::NetworkFilterFactoriesList> createNetworkFilterFactoryList(
      const Protobuf::RepeatedPtrField<envoy::config::listener::v3::Filter>& filters,
      Server::Configuration::FilterChainFactoryContext& filter_chain_factory_context) override {
    return ProdListenerComponentFactory::createNetworkFilterFactoryListImpl(
        filters, filter_chain_factory_context, network_config_provider_manager_);
  }
  absl::StatusOr<Filter::ListenerFilterFactoriesList> createListenerFilterFactoryList(
      const Protobuf::RepeatedPtrField<envoy::config::listener::v3::ListenerFilter>& filters,
      Configuration::ListenerFactoryContext& context) override {
    return ProdListenerComponentFactory::createListenerFilterFactoryListImpl(
        filters, context, tcp_listener_config_provider_manager_);
  }
  absl::StatusOr<std::vector<Network::UdpListenerFilterFactoryCb>>
  createUdpListenerFilterFactoryList(
      const Protobuf::RepeatedPtrField<envoy::config::listener::v3::ListenerFilter>& filters,
      Configuration::ListenerFactoryContext& context) override {
    return ProdListenerComponentFactory::createUdpListenerFilterFactoryListImpl(filters, context);
  }
  absl::StatusOr<Filter::QuicListenerFilterFactoriesList> createQuicListenerFilterFactoryList(
      const Protobuf::RepeatedPtrField<envoy::config::listener::v3::ListenerFilter>& filters,
      Configuration::ListenerFactoryContext& context) override {
    return ProdListenerComponentFactory::createQuicListenerFilterFactoryListImpl(
        filters, context, quic_listener_config_provider_manager_);
  }
  absl::StatusOr<Network::SocketSharedPtr>
  createListenSocket(Network::Address::InstanceConstSharedPtr, Network::Socket::Type,
                     const Network::Socket::OptionsSharedPtr&, ListenerComponentFactory::BindType,
                     const Network::SocketCreationOptions&, uint32_t) override {
    // Returned sockets are not currently used so we can return nothing here safely vs. a
    // validation mock.
    // TODO(mattklein123): The fact that this returns nullptr makes the production code more
    // convoluted than it needs to be. Fix this to return a mock in a follow up.
    return nullptr;
  }
  DrainManagerPtr createDrainManager(envoy::config::listener::v3::Listener::DrainType) override {
    return nullptr;
  }
  uint64_t nextListenerTag() override { return 0; }
  Filter::TcpListenerFilterConfigProviderManagerImpl*
  getTcpListenerConfigProviderManager() override {
    return &tcp_listener_config_provider_manager_;
  }

private:
  Filter::NetworkFilterConfigProviderManagerImpl network_config_provider_manager_;
  Filter::TcpListenerFilterConfigProviderManagerImpl tcp_listener_config_provider_manager_;
  Filter::QuicListenerFilterConfigProviderManagerImpl quic_listener_config_provider_manager_;
  Instance& parent_;
};

class ValidationListenerManagerFactoryImpl : public ListenerManagerFactory {
public:
  std::unique_ptr<ListenerManager>
  createListenerManager(Instance& server, std::unique_ptr<ListenerComponentFactory>&& factory,
                        WorkerFactory& worker_factory, bool enable_dispatcher_stats,
                        Quic::QuicStatNames& quic_stat_names) override {
    ASSERT(!factory);
    return std::make_unique<ListenerManagerImpl>(
        server, std::make_unique<ValidationListenerComponentFactory>(server), worker_factory,
        enable_dispatcher_stats, quic_stat_names);
  }
  std::string name() const override {
    return Config::ServerExtensionValues::get().VALIDATION_LISTENER;
  }
  ProtobufTypes::MessagePtr createEmptyConfigProto() override {
    return std::make_unique<envoy::config::listener::v3::ValidationListenerManager>();
  }
};

DECLARE_FACTORY(ValidationListenerManagerFactoryImpl);

} // namespace Server
} // namespace Envoy
