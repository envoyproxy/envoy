#include <unistd.h>

#include <cstdint>
#include <memory>
#include <string>

#include "common/api/api_impl.h"
#include "common/common/fmt.h"
#include "common/filesystem/filesystem_impl.h"
#include "common/protobuf/utility.h"

#include "server/config_validation/server.h"
#include "server/configuration_impl.h"
#include "server/options_impl.h"

#include "test/integration/server.h"
#include "test/mocks/server/mocks.h"
#include "test/mocks/ssl/mocks.h"
#include "test/test_common/threadsafe_singleton_injector.h"
#include "test/test_common/utility.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::_;
using testing::Invoke;
using testing::NiceMock;
using testing::Return;
using testing::ReturnRef;

namespace Envoy {
namespace ConfigTest {

namespace {

// asConfigYaml returns a new config that empties the configPath() and populates configYaml()
OptionsImpl asConfigYaml(const OptionsImpl& src) {
  return Envoy::Server::createTestOptionsImpl("", Filesystem::fileReadToEnd(src.configPath()),
                                              src.localAddressIpVersion());
}

} // namespace

class ConfigTest {
public:
  ConfigTest(const OptionsImpl& options) : options_(options) {
    ON_CALL(server_, options()).WillByDefault(ReturnRef(options_));
    ON_CALL(server_, random()).WillByDefault(ReturnRef(random_));
    ON_CALL(server_, sslContextManager()).WillByDefault(ReturnRef(ssl_context_manager_));
    ON_CALL(server_.api_, fileReadToEnd("lightstep_access_token"))
        .WillByDefault(Return("access_token"));

    envoy::config::bootstrap::v2::Bootstrap bootstrap;
    Server::InstanceUtil::loadBootstrapConfig(bootstrap, options_);
    Server::Configuration::InitialImpl initial_config(bootstrap);
    Server::Configuration::MainImpl main_config;

    cluster_manager_factory_ = std::make_unique<Upstream::ValidationClusterManagerFactory>(
        server_.runtime(), server_.stats(), server_.threadLocal(), server_.random(),
        server_.dnsResolver(), ssl_context_manager_, server_.dispatcher(), server_.localInfo(),
        server_.secretManager(), api_);

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

  Api::Impl api_;
  NiceMock<Server::MockInstance> server_;
  NiceMock<Ssl::MockContextManager> ssl_context_manager_;
  OptionsImpl options_;
  std::unique_ptr<Upstream::ProdClusterManagerFactory> cluster_manager_factory_;
  NiceMock<Server::MockListenerComponentFactory> component_factory_;
  NiceMock<Server::MockWorkerFactory> worker_factory_;
  Server::ListenerManagerImpl listener_manager_{server_, component_factory_, worker_factory_,
                                                server_.timeSystem()};
  Runtime::RandomGeneratorImpl random_;
  NiceMock<Api::MockOsSysCalls> os_sys_calls_;
  TestThreadsafeSingletonInjector<Api::OsSysCallsImpl> os_calls{&os_sys_calls_};
};

void testMerge() {
  const std::string overlay = "static_resources: { clusters: [{name: 'foo'}]}";
  OptionsImpl options(Server::createTestOptionsImpl("google_com_proxy.v2.yaml", overlay,
                                                    Network::Address::IpVersion::v6));
  envoy::config::bootstrap::v2::Bootstrap bootstrap;
  Server::InstanceUtil::loadBootstrapConfig(bootstrap, options);
  EXPECT_EQ(2, bootstrap.static_resources().clusters_size());
}

void testIncompatibleMerge() {
  const std::string overlay = "static_resources: { clusters: [{name: 'foo'}]}";
  OptionsImpl options(Server::createTestOptionsImpl("google_com_proxy.v1.yaml", overlay,
                                                    Network::Address::IpVersion::v6));
  envoy::config::bootstrap::v2::Bootstrap bootstrap;
  EXPECT_THROW_WITH_MESSAGE(Server::InstanceUtil::loadBootstrapConfig(bootstrap, options),
                            EnvoyException,
                            "V1 config (detected) with --config-yaml is not supported");
}

uint32_t run(const std::string& directory) {
  uint32_t num_tested = 0;
  for (const std::string& filename : TestUtility::listFiles(directory, false)) {
    OptionsImpl options(
        Envoy::Server::createTestOptionsImpl(filename, "", Network::Address::IpVersion::v6));
    ConfigTest test1(options);
    // Config flag --config-yaml is only supported for v2 configs.
    envoy::config::bootstrap::v2::Bootstrap bootstrap;
    if (Server::InstanceUtil::loadBootstrapConfig(bootstrap, options) ==
        Server::InstanceUtil::BootstrapVersion::V2) {
      ConfigTest test2(asConfigYaml(options));
    }
    num_tested++;
  }
  return num_tested;
}

} // namespace ConfigTest
} // namespace Envoy
