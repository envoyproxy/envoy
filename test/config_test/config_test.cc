#include <cstdint>
#include <string>

#include "server/configuration_impl.h"

#include "test/integration/server.h"
#include "test/mocks/server/mocks.h"
#include "test/mocks/ssl/mocks.h"
#include "test/test_common/utility.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "spdlog/spdlog.h"

namespace Envoy {
using testing::_;
using testing::Invoke;
using testing::NiceMock;
using testing::Return;
using testing::ReturnRef;

namespace ConfigTest {

class ConfigTest {
public:
  ConfigTest(const std::string& file_path)
      : options_(file_path),
        cluster_manager_factory_(server_.runtime(), server_.stats(), server_.threadLocal(),
                                 server_.random(), server_.dnsResolver(), ssl_context_manager_,
                                 server_.dispatcher(), server_.localInfo()) {
    ON_CALL(server_, options()).WillByDefault(ReturnRef(options_));
    ON_CALL(server_, sslContextManager()).WillByDefault(ReturnRef(ssl_context_manager_));
    ON_CALL(server_.api_, fileReadToEnd("lightstep_access_token"))
        .WillByDefault(Return("access_token"));

    Json::ObjectSharedPtr config_json = Json::Factory::loadFromFile(file_path);
    Server::Configuration::InitialImpl initial_config(*config_json);
    Server::Configuration::MainImpl main_config(server_, cluster_manager_factory_);

    ON_CALL(server_, clusterManager())
        .WillByDefault(
            Invoke([&]() -> Upstream::ClusterManager& { return main_config.clusterManager(); }));

    try {
      main_config.initialize(*config_json);
    } catch (const EnvoyException& ex) {
      ADD_FAILURE() << fmt::format("'{}' config failed. Error: {}", file_path, ex.what());
    }

    server_.thread_local_.shutdownThread();
  }

  NiceMock<Server::MockInstance> server_;
  NiceMock<Ssl::MockContextManager> ssl_context_manager_;
  Server::TestOptionsImpl options_;
  Upstream::ProdClusterManagerFactory cluster_manager_factory_;
};

uint32_t run(const std::string& directory) {

  uint32_t num_tested = 0;
  for (const std::string& filename : TestUtility::listFiles(directory, true)) {
    ConfigTest config(filename);
    num_tested++;
  }
  return num_tested;
}

} // ConfigTest
} // Envoy
