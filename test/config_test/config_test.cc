#include <cstdint>
#include <memory>
#include <string>

#include "envoy/common/platform.h"
#include "envoy/config/bootstrap/v3/bootstrap.pb.h"
#include "envoy/config/listener/v3/listener_components.pb.h"

#include "source/common/api/os_sys_calls_impl.h"
#include "source/common/common/fmt.h"
#include "source/common/listener_manager/listener_manager_impl.h"
#include "source/common/protobuf/utility.h"
#include "source/common/runtime/runtime_features.h"
#include "source/server/config_validation/server.h"
#include "source/server/configuration_impl.h"
#include "source/server/options_impl.h"

#include "test/integration/server.h"
#include "test/mocks/server/instance.h"
#include "test/mocks/server/listener_component_factory.h"
#include "test/mocks/server/worker.h"
#include "test/mocks/server/worker_factory.h"
#include "test/mocks/ssl/mocks.h"
#include "test/test_common/simulated_time_system.h"
#include "test/test_common/threadsafe_singleton_injector.h"
#include "test/test_common/utility.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::_;
using testing::AtLeast;
using testing::Invoke;
using testing::NiceMock;
using testing::Return;
using testing::ReturnRef;

namespace Envoy {
namespace ConfigTest {
namespace {

// asConfigYaml returns a new config that empties the configPath() and populates configYaml()
OptionsImplBase asConfigYaml(const OptionsImplBase& src, Api::Api& api) {
  return Envoy::Server::createTestOptionsImpl(
      "", api.fileSystem().fileReadToEnd(src.configPath()).value(), src.localAddressIpVersion());
}

static std::vector<absl::string_view> unsuported_win32_configs = {
#if defined(WIN32)
    "rbac_envoy.yaml",
#endif
#if defined(WIN32) && !defined(SO_ORIGINAL_DST)
    "configs_original-dst-cluster_proxy_config.yaml"
#endif
};

} // namespace

class ConfigTest {
public:
  ConfigTest(OptionsImplBase& options)
      : api_(Api::createApiForTest(time_system_)), options_(options) {
    ON_CALL(*server_.server_factory_context_, api()).WillByDefault(ReturnRef(server_.api_));
    ON_CALL(server_, options()).WillByDefault(ReturnRef(options_));
    ON_CALL(server_, sslContextManager()).WillByDefault(ReturnRef(ssl_context_manager_));
    ON_CALL(server_.api_, fileSystem()).WillByDefault(ReturnRef(file_system_));
    ON_CALL(server_.api_, randomGenerator()).WillByDefault(ReturnRef(random_));
    ON_CALL(server_.api_, threadFactory()).WillByDefault(Invoke([&]() -> Thread::ThreadFactory& {
      return api_->threadFactory();
    }));
    ON_CALL(file_system_, fileReadToEnd(_))
        .WillByDefault(Invoke([&](const std::string& file) -> absl::StatusOr<std::string> {
          return api_->fileSystem().fileReadToEnd(file);
        }));
    ON_CALL(os_sys_calls_, close(_)).WillByDefault(Return(Api::SysCallIntResult{0, 0}));
    EXPECT_CALL(os_sys_calls_, getaddrinfo(_, _, _, _))
        .WillRepeatedly(Invoke([&](const char* node, const char* service,
                                   const struct addrinfo* hints, struct addrinfo** res) {
          Api::OsSysCallsImpl real;
          return real.getaddrinfo(node, service, hints, res);
        }));

    // Here we setup runtime to mimic the actual deprecated feature list used in the
    // production code. Note that this test is actually more strict than production because
    // in production runtime is not setup until after the bootstrap config is loaded. This seems
    // better for configuration tests.
    ON_CALL(server_.runtime_loader_.snapshot_, deprecatedFeatureEnabled(_, _))
        .WillByDefault(Invoke([](absl::string_view, bool default_value) { return default_value; }));

    ON_CALL(server_.runtime_loader_, threadsafeSnapshot()).WillByDefault(Invoke([this]() {
      return snapshot_;
    }));

    // For configuration/example tests we don't fail if WIP APIs are used.
    EXPECT_CALL(server_.validation_context_.static_validation_visitor_, onWorkInProgress(_))
        .Times(AtLeast(0));
    EXPECT_CALL(server_.validation_context_.dynamic_validation_visitor_, onWorkInProgress(_))
        .Times(AtLeast(0));

    envoy::config::bootstrap::v3::Bootstrap bootstrap;
    EXPECT_TRUE(Server::InstanceUtil::loadBootstrapConfig(
                    bootstrap, options_,
                    server_.messageValidationContext().staticValidationVisitor(), *api_)
                    .ok());
    absl::Status creation_status;
    Server::Configuration::InitialImpl initial_config(bootstrap, creation_status);
    THROW_IF_NOT_OK_REF(creation_status);
    Server::Configuration::MainImpl main_config;

    // Emulate main implementation of initializing bootstrap extensions.
    std::vector<Server::BootstrapExtensionPtr> bootstrap_extensions;
    for (const auto& bootstrap_extension : bootstrap.bootstrap_extensions()) {
      auto& factory =
          Config::Utility::getAndCheckFactory<Server::Configuration::BootstrapExtensionFactory>(
              bootstrap_extension);
      auto config = Config::Utility::translateAnyToFactoryConfig(
          bootstrap_extension.typed_config(),
          server_.messageValidationContext().staticValidationVisitor(), factory);
      bootstrap_extensions.push_back(
          factory.createBootstrapExtension(*config, server_factory_context_));
    }

    cluster_manager_factory_ = std::make_unique<Upstream::ValidationClusterManagerFactory>(
        *server_.server_factory_context_, server_.stats(), server_.threadLocal(),
        server_.httpContext(),
        [this]() -> Network::DnsResolverSharedPtr { return this->server_.dnsResolver(); },
        ssl_context_manager_, server_.secretManager(), server_.quic_stat_names_, server_);

    ON_CALL(server_, clusterManager()).WillByDefault(Invoke([&]() -> Upstream::ClusterManager& {
      return *main_config.clusterManager();
    }));
    ON_CALL(server_, listenerManager()).WillByDefault(ReturnRef(listener_manager_));
    ON_CALL(component_factory_, createNetworkFilterFactoryList(_, _))
        .WillByDefault(Invoke(
            [&](const Protobuf::RepeatedPtrField<envoy::config::listener::v3::Filter>& filters,
                Server::Configuration::FilterChainFactoryContext& context) {
              return Server::ProdListenerComponentFactory::createNetworkFilterFactoryListImpl(
                  filters, context, network_config_provider_manager_);
            }));
    ON_CALL(component_factory_, getTcpListenerConfigProviderManager())
        .WillByDefault(Return(&tcp_listener_config_provider_manager_));
    ON_CALL(component_factory_, createListenerFilterFactoryList(_, _))
        .WillByDefault(Invoke(
            [&](const Protobuf::RepeatedPtrField<envoy::config::listener::v3::ListenerFilter>&
                    filters,
                Server::Configuration::ListenerFactoryContext& context) {
              return Server::ProdListenerComponentFactory::createListenerFilterFactoryListImpl(
                  filters, context, *component_factory_.getTcpListenerConfigProviderManager());
            }));
    ON_CALL(component_factory_, createUdpListenerFilterFactoryList(_, _))
        .WillByDefault(Invoke(
            [&](const Protobuf::RepeatedPtrField<envoy::config::listener::v3::ListenerFilter>&
                    filters,
                Server::Configuration::ListenerFactoryContext& context) {
              return Server::ProdListenerComponentFactory::createUdpListenerFilterFactoryListImpl(
                  filters, context);
            }));
    ON_CALL(server_, serverFactoryContext()).WillByDefault(ReturnRef(server_factory_context_));

    try {
      THROW_IF_NOT_OK(main_config.initialize(bootstrap, server_, *cluster_manager_factory_));
    } catch (const EnvoyException& ex) {
      ADD_FAILURE() << fmt::format("'{}' config failed. Error: {}", options_.configPath(),
                                   ex.what());
    }

    server_.thread_local_.shutdownThread();
  }

  Event::SimulatedTimeSystem time_system_;
  Api::ApiPtr api_;
  NiceMock<Server::MockInstance> server_;
  Server::ServerFactoryContextImpl server_factory_context_{server_};
  NiceMock<Ssl::MockContextManager> ssl_context_manager_;
  OptionsImplBase& options_;
  std::unique_ptr<Upstream::ProdClusterManagerFactory> cluster_manager_factory_;
  std::unique_ptr<NiceMock<Server::MockListenerComponentFactory>> component_factory_ptr_{
      std::make_unique<NiceMock<Server::MockListenerComponentFactory>>()};
  NiceMock<Server::MockListenerComponentFactory>& component_factory_{*component_factory_ptr_};
  NiceMock<Server::MockWorkerFactory> worker_factory_;
  Server::ListenerManagerImpl listener_manager_{server_, std::move(component_factory_ptr_),
                                                worker_factory_, false, server_.quic_stat_names_};
  Random::RandomGeneratorImpl random_;
  std::shared_ptr<Runtime::MockSnapshot> snapshot_{
      std::make_shared<NiceMock<Runtime::MockSnapshot>>()};
  NiceMock<Api::MockOsSysCalls> os_sys_calls_;
  TestThreadsafeSingletonInjector<Api::OsSysCallsImpl> os_calls{&os_sys_calls_};
  NiceMock<Filesystem::MockInstance> file_system_;
  Filter::NetworkFilterConfigProviderManagerImpl network_config_provider_manager_;
  Filter::TcpListenerFilterConfigProviderManagerImpl tcp_listener_config_provider_manager_;
};

void testMerge() {
  Api::ApiPtr api = Api::createApiForTest();
  const std::string overlay = R"EOF(
        {
          admin: {
            "address": {
              "socket_address": {
                "address": "1.2.3.4",
                "port_value": 5678
              }
            }
          },
          static_resources: {
            clusters: [
              {
                name: 'foo'
              }
            ]
          }
        })EOF";
  OptionsImplBase options(Server::createTestOptionsImpl("envoyproxy_io_proxy.yaml", overlay,
                                                        Network::Address::IpVersion::v6));
  envoy::config::bootstrap::v3::Bootstrap bootstrap;
  ASSERT_TRUE(Server::InstanceUtil::loadBootstrapConfig(
                  bootstrap, options, ProtobufMessage::getStrictValidationVisitor(), *api)
                  .ok());
  EXPECT_EQ(2, bootstrap.static_resources().clusters_size());
}

uint32_t run(const std::string& directory) {
  uint32_t num_tested = 0;
  Api::ApiPtr api = Api::createApiForTest();
  for (const std::string& filename : TestUtility::listFiles(directory, false)) {
#ifndef ENVOY_ENABLE_QUIC
    if (filename.find("http3") != std::string::npos) {
      ENVOY_LOG_MISC(info, "Skipping HTTP/3 config {}.\n", filename);
      num_tested++;
      continue;
    }
#endif

    ENVOY_LOG_MISC(info, "testing {}.\n", filename);
    if (std::find_if(unsuported_win32_configs.begin(), unsuported_win32_configs.end(),
                     [filename](const absl::string_view& s) {
                       return filename.find(std::string(s)) != std::string::npos;
                     }) == unsuported_win32_configs.end()) {
      OptionsImplBase options(
          Envoy::Server::createTestOptionsImpl(filename, "", Network::Address::IpVersion::v6));
      ConfigTest test1(options);
      envoy::config::bootstrap::v3::Bootstrap bootstrap;
      EXPECT_TRUE(Server::InstanceUtil::loadBootstrapConfig(
                      bootstrap, options, ProtobufMessage::getStrictValidationVisitor(), *api)
                      .ok());
      ENVOY_LOG_MISC(info, "testing {} as yaml.", filename);
      OptionsImplBase config = asConfigYaml(options, *api);
      ConfigTest test2(config);
    }
    num_tested++;
  }
  return num_tested;
}
} // namespace ConfigTest
} // namespace Envoy
