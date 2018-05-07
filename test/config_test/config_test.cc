#include <unistd.h>

#include <cstdint>
#include <string>

#include "common/common/fmt.h"
#include "common/protobuf/utility.h"

#include "server/config_validation/server.h"
#include "server/configuration_impl.h"

#include "test/integration/server.h"
#include "test/mocks/server/mocks.h"
#include "test/mocks/ssl/mocks.h"
#include "test/test_common/threadsafe_singleton_injector.h"
#include "test/test_common/utility.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::Invoke;
using testing::NiceMock;
using testing::Return;
using testing::ReturnRef;
using testing::_;

namespace Envoy {
namespace ConfigTest {

class ConfigTest {
public:
  ConfigTest(const Server::TestOptionsImpl& options) : options_(options) {
    ON_CALL(server_, options()).WillByDefault(ReturnRef(options_));
    ON_CALL(server_, random()).WillByDefault(ReturnRef(random_));
    ON_CALL(server_, sslContextManager()).WillByDefault(ReturnRef(ssl_context_manager_));
    ON_CALL(server_.api_, fileReadToEnd("lightstep_access_token"))
        .WillByDefault(Return("access_token"));

    envoy::config::bootstrap::v2::Bootstrap bootstrap;
    Server::InstanceUtil::loadBootstrapConfig(bootstrap, options_);
    Server::Configuration::InitialImpl initial_config(bootstrap);
    Server::Configuration::MainImpl main_config;

    cluster_manager_factory_.reset(new Upstream::ValidationClusterManagerFactory(
        server_.runtime(), server_.stats(), server_.threadLocal(), server_.random(),
        server_.dnsResolver(), ssl_context_manager_, server_.dispatcher(), server_.localInfo()));

    ON_CALL(server_, clusterManager()).WillByDefault(Invoke([&]() -> Upstream::ClusterManager& {
      return *main_config.clusterManager();
    }));
    ON_CALL(server_, listenerManager()).WillByDefault(ReturnRef(listener_manager_));
    ON_CALL(component_factory_, createNetworkFilterFactoryList(_, _))
        .WillByDefault(
            Invoke([&](const Protobuf::RepeatedPtrField<envoy::api::v2::listener::Filter>& filters,
                       Server::Configuration::FactoryContext& context)
                       -> std::vector<Network::FilterFactoryCb> {
              return Server::ProdListenerComponentFactory::createNetworkFilterFactoryList_(filters,
                                                                                           context);
            }));
    ON_CALL(component_factory_, createListenerFilterFactoryList(_, _))
        .WillByDefault(Invoke(
            [&](const Protobuf::RepeatedPtrField<envoy::api::v2::listener::ListenerFilter>& filters,
                Server::Configuration::ListenerFactoryContext& context)
                -> std::vector<Network::ListenerFilterFactoryCb> {
              return Server::ProdListenerComponentFactory::createListenerFilterFactoryList_(
                  filters, context);
            }));

    try {
      main_config.initialize(bootstrap, server_, *cluster_manager_factory_);
    } catch (const EnvoyException& ex) {
      ADD_FAILURE() << fmt::format("'{}' config failed. Error: {}", options_.configPath(),
                                   ex.what());
    }

    server_.thread_local_.shutdownThread();
  }

  NiceMock<Server::MockInstance> server_;
  NiceMock<Ssl::MockContextManager> ssl_context_manager_;
  Server::TestOptionsImpl options_;
  std::unique_ptr<Upstream::ProdClusterManagerFactory> cluster_manager_factory_;
  NiceMock<Server::MockListenerComponentFactory> component_factory_;
  NiceMock<Server::MockWorkerFactory> worker_factory_;
  Server::ListenerManagerImpl listener_manager_{server_, component_factory_, worker_factory_};
  Runtime::RandomGeneratorImpl random_;
  NiceMock<Api::MockOsSysCalls> os_sys_calls_;
  TestThreadsafeSingletonInjector<Api::OsSysCallsImpl> os_calls{&os_sys_calls_};
};

void testMerge() {
  const std::string overlay = "static_resources: { clusters: [{name: 'foo'}]}";
  Server::TestOptionsImpl options("google_com_proxy.v2.yaml", overlay,
                                  Network::Address::IpVersion::v6);
  envoy::config::bootstrap::v2::Bootstrap bootstrap;
  Server::InstanceUtil::loadBootstrapConfig(bootstrap, options);
  EXPECT_EQ(2, bootstrap.static_resources().clusters_size());
}

void testIncompatibleMerge() {
  const std::string overlay = "static_resources: { clusters: [{name: 'foo'}]}";
  Server::TestOptionsImpl options("google_com_proxy.v1.yaml", overlay,
                                  Network::Address::IpVersion::v6);
  envoy::config::bootstrap::v2::Bootstrap bootstrap;
  EXPECT_THROW_WITH_MESSAGE(Server::InstanceUtil::loadBootstrapConfig(bootstrap, options),
                            EnvoyException,
                            "V1 config (detected) with --config-yaml is not supported");
}

uint32_t run(const std::string& directory) {
  uint32_t num_tested = 0;
  for (const std::string& filename : TestUtility::listFiles(directory, false)) {
    Server::TestOptionsImpl options(filename, Network::Address::IpVersion::v6);
    ConfigTest test1(options);
    // Config flag --config-yaml is only supported for v2 configs.
    envoy::config::bootstrap::v2::Bootstrap bootstrap;
    if (Server::InstanceUtil::loadBootstrapConfig(bootstrap, options) ==
        Server::InstanceUtil::BootstrapVersion::V2) {
      ConfigTest test2(options.asConfigYaml());
    }
    num_tested++;
  }
  return num_tested;
}

} // namespace ConfigTest
} // namespace Envoy
