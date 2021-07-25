#include <cstdint>
#include <memory>
#include <string>

#include "envoy/common/platform.h"
#include "envoy/config/bootstrap/v3/bootstrap.pb.h"
#include "envoy/config/listener/v3/listener_components.pb.h"

#include "source/common/common/fmt.h"
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
using testing::Invoke;
using testing::NiceMock;
using testing::Return;
using testing::ReturnRef;
using testing::StrEq;
using testing::StrNe;

namespace Envoy {
namespace ConfigTest {
namespace {

// asConfigYaml returns a new config that empties the configPath() and populates configYaml()
OptionsImpl asConfigYaml(const OptionsImpl& src, Api::Api& api) {
  return Envoy::Server::createTestOptionsImpl("", api.fileSystem().fileReadToEnd(src.configPath()),
                                              src.localAddressIpVersion());
}

static std::vector<absl::string_view> unsuported_win32_configs = {
#if defined(WIN32) && !defined(SO_ORIGINAL_DST)
    "configs_original-dst-cluster_proxy_config.yaml"
#endif
};

class ScopedRuntimeInjector {
public:
  ScopedRuntimeInjector(Runtime::Loader& runtime) {
    Runtime::LoaderSingleton::initialize(&runtime);
  } // namespace

  ~ScopedRuntimeInjector() { Runtime::LoaderSingleton::clear(); }
}; // namespace ConfigTest

} // namespace

class ConfigTest {
public:
  ConfigTest(const OptionsImpl& options)
      : api_(Api::createApiForTest(time_system_)), options_(options) {
    ON_CALL(server_, options()).WillByDefault(ReturnRef(options_));
    ON_CALL(server_, sslContextManager()).WillByDefault(ReturnRef(ssl_context_manager_));
    ON_CALL(server_.api_, fileSystem()).WillByDefault(ReturnRef(file_system_));
    ON_CALL(server_.api_, randomGenerator()).WillByDefault(ReturnRef(random_));
    ON_CALL(file_system_, fileReadToEnd(StrEq("/etc/envoy/lightstep_access_token")))
        .WillByDefault(Return("access_token"));
    ON_CALL(file_system_, fileReadToEnd(StrNe("/etc/envoy/lightstep_access_token")))
        .WillByDefault(Invoke([&](const std::string& file) -> std::string {
          return api_->fileSystem().fileReadToEnd(file);
        }));
    ON_CALL(os_sys_calls_, close(_)).WillByDefault(Return(Api::SysCallIntResult{0, 0}));

    // Here we setup runtime to mimic the actual deprecated feature list used in the
    // production code. Note that this test is actually more strict than production because
    // in production runtime is not setup until after the bootstrap config is loaded. This seems
    // better for configuration tests.
    ScopedRuntimeInjector scoped_runtime(server_.runtime());
    ON_CALL(server_.runtime_loader_.snapshot_, deprecatedFeatureEnabled(_, _))
        .WillByDefault(Invoke([](absl::string_view, bool default_value) { return default_value; }));

    // TODO(snowp): There's no way to override runtime flags per example file (since we mock out the
    // runtime loader), so temporarily enable this flag explicitly here until we flip the default.
    // This should allow the existing configuration examples to continue working despite the feature
    // being disabled by default.
    ON_CALL(*snapshot_,
            runtimeFeatureEnabled("envoy.reloadable_features.experimental_matching_api"))
        .WillByDefault(Return(true));
    ON_CALL(server_.runtime_loader_, threadsafeSnapshot()).WillByDefault(Invoke([this]() {
      return snapshot_;
    }));

    envoy::config::bootstrap::v3::Bootstrap bootstrap;
    Server::InstanceUtil::loadBootstrapConfig(
        bootstrap, options_, server_.messageValidationContext().staticValidationVisitor(), *api_);
    Server::Configuration::InitialImpl initial_config(bootstrap, options);
    Server::Configuration::MainImpl main_config;

    cluster_manager_factory_ = std::make_unique<Upstream::ValidationClusterManagerFactory>(
        server_.admin(), server_.runtime(), server_.stats(), server_.threadLocal(),
        server_.dnsResolver(), ssl_context_manager_, server_.dispatcher(), server_.localInfo(),
        server_.secretManager(), server_.messageValidationContext(), *api_, server_.httpContext(),
        server_.grpcContext(), server_.routerContext(), server_.accessLogManager(),
        server_.singletonManager(), server_.options(), server_.quic_stat_names_);

    ON_CALL(server_, clusterManager()).WillByDefault(Invoke([&]() -> Upstream::ClusterManager& {
      return *main_config.clusterManager();
    }));
    ON_CALL(server_, listenerManager()).WillByDefault(ReturnRef(listener_manager_));
    ON_CALL(component_factory_, createNetworkFilterFactoryList(_, _))
        .WillByDefault(Invoke(
            [&](const Protobuf::RepeatedPtrField<envoy::config::listener::v3::Filter>& filters,
                Server::Configuration::FilterChainFactoryContext& context)
                -> std::vector<Network::FilterFactoryCb> {
              return Server::ProdListenerComponentFactory::createNetworkFilterFactoryList_(filters,
                                                                                           context);
            }));
    ON_CALL(component_factory_, createListenerFilterFactoryList(_, _))
        .WillByDefault(Invoke(
            [&](const Protobuf::RepeatedPtrField<envoy::config::listener::v3::ListenerFilter>&
                    filters,
                Server::Configuration::ListenerFactoryContext& context)
                -> std::vector<Network::ListenerFilterFactoryCb> {
              return Server::ProdListenerComponentFactory::createListenerFilterFactoryList_(
                  filters, context);
            }));
    ON_CALL(component_factory_, createUdpListenerFilterFactoryList(_, _))
        .WillByDefault(Invoke(
            [&](const Protobuf::RepeatedPtrField<envoy::config::listener::v3::ListenerFilter>&
                    filters,
                Server::Configuration::ListenerFactoryContext& context)
                -> std::vector<Network::UdpListenerFilterFactoryCb> {
              return Server::ProdListenerComponentFactory::createUdpListenerFilterFactoryList_(
                  filters, context);
            }));
    ON_CALL(server_, serverFactoryContext()).WillByDefault(ReturnRef(server_factory_context_));

    try {
      main_config.initialize(bootstrap, server_, *cluster_manager_factory_);
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
  OptionsImpl options_;
  std::unique_ptr<Upstream::ProdClusterManagerFactory> cluster_manager_factory_;
  NiceMock<Server::MockListenerComponentFactory> component_factory_;
  NiceMock<Server::MockWorkerFactory> worker_factory_;
  Server::ListenerManagerImpl listener_manager_{server_, component_factory_, worker_factory_, false,
                                                server_.quic_stat_names_};
  Random::RandomGeneratorImpl random_;
  std::shared_ptr<Runtime::MockSnapshot> snapshot_{
      std::make_shared<NiceMock<Runtime::MockSnapshot>>()};
  NiceMock<Api::MockOsSysCalls> os_sys_calls_;
  TestThreadsafeSingletonInjector<Api::OsSysCallsImpl> os_calls{&os_sys_calls_};
  NiceMock<Filesystem::MockInstance> file_system_;
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
  OptionsImpl options(Server::createTestOptionsImpl("envoyproxy_io_proxy.yaml", overlay,
                                                    Network::Address::IpVersion::v6));
  envoy::config::bootstrap::v3::Bootstrap bootstrap;
  Server::InstanceUtil::loadBootstrapConfig(bootstrap, options,
                                            ProtobufMessage::getStrictValidationVisitor(), *api);
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
      OptionsImpl options(
          Envoy::Server::createTestOptionsImpl(filename, "", Network::Address::IpVersion::v6));
      ConfigTest test1(options);
      envoy::config::bootstrap::v3::Bootstrap bootstrap;
      Server::InstanceUtil::loadBootstrapConfig(
          bootstrap, options, ProtobufMessage::getStrictValidationVisitor(), *api);
      ENVOY_LOG_MISC(info, "testing {} as yaml.", filename);
      ConfigTest test2(asConfigYaml(options, *api));
    }
    num_tested++;
  }
  return num_tested;
}

void loadVersionedBootstrapFile(const std::string& filename,
                                envoy::config::bootstrap::v3::Bootstrap& bootstrap_message,
                                absl::optional<uint32_t> bootstrap_version) {
  Api::ApiPtr api = Api::createApiForTest();
  OptionsImpl options(
      Envoy::Server::createTestOptionsImpl(filename, "", Network::Address::IpVersion::v6));
  // Avoid contention issues with other tests over the hot restart domain socket.
  options.setHotRestartDisabled(true);
  if (bootstrap_version.has_value()) {
    options.setBootstrapVersion(*bootstrap_version);
  }
  Server::InstanceUtil::loadBootstrapConfig(bootstrap_message, options,
                                            ProtobufMessage::getStrictValidationVisitor(), *api);
}

void loadBootstrapConfigProto(const envoy::config::bootstrap::v3::Bootstrap& in_proto,
                              envoy::config::bootstrap::v3::Bootstrap& bootstrap_message) {
  Api::ApiPtr api = Api::createApiForTest();
  OptionsImpl options(
      Envoy::Server::createTestOptionsImpl("", "", Network::Address::IpVersion::v6));
  options.setConfigProto(in_proto);
  // Avoid contention issues with other tests over the hot restart domain socket.
  options.setHotRestartDisabled(true);
  Server::InstanceUtil::loadBootstrapConfig(bootstrap_message, options,
                                            ProtobufMessage::getStrictValidationVisitor(), *api);
}

} // namespace ConfigTest
} // namespace Envoy
