#include "server/configuration_impl.h"

#include "test/integration/server.h"
#include "test/mocks/server/mocks.h"
#include "test/mocks/ssl/mocks.h"
#include "test/test_common/environment.h"

#include <dirent.h>

using testing::_;
using testing::Invoke;
using testing::NiceMock;
using testing::Return;
using testing::ReturnRef;

class ConfigTest {
public:
  ConfigTest(const std::string& file_path) : options_(file_path) {
    ON_CALL(server_, options()).WillByDefault(ReturnRef(options_));
    ON_CALL(server_, sslContextManager()).WillByDefault(ReturnRef(ssl_context_manager_));
    ON_CALL(server_.api_, fileReadToEnd("lightstep_access_token"))
        .WillByDefault(Return("access_token"));

    Json::ObjectPtr config_json = Json::Factory::LoadFromFile(file_path);
    Server::Configuration::InitialImpl initial_config(*config_json);
    Server::Configuration::MainImpl main_config(server_);

    ON_CALL(server_, clusterManager())
        .WillByDefault(
            Invoke([&]() -> Upstream::ClusterManager& { return main_config.clusterManager(); }));

    try {
      main_config.initialize(*config_json);
    } catch (const EnvoyException& ex) {
      throw EnvoyException(fmt::format("'{}' config failed. Error: {}", file_path, ex.what()));
    }

    server_.thread_local_.shutdownThread();
  }

  NiceMock<Server::MockInstance> server_;
  NiceMock<Ssl::MockContextManager> ssl_context_manager_;
  Server::TestOptionsImpl options_;
};

uint32_t runConfigTest() {
  uint32_t num_tested = 0;
  DIR* dir = opendir(TestEnvironment::temporaryDirectory().c_str());
  if (!dir) {
    throw std::runtime_error("Generated configs directory not found");
  }
  dirent* entry;
  while ((entry = readdir(dir)) != nullptr) {
    if (entry->d_type != DT_REG) {
      continue;
    }

    std::string file_name =
        fmt::format("{}/{}", TestEnvironment::temporaryDirectory(), std::string(entry->d_name));
    Logger::Registry::getLog(Logger::Id::testing).info("testing config: {}", file_name);
    ConfigTest config(file_name);
    num_tested++;
  }

  closedir(dir);
  return num_tested;
}

TEST(ExampleConfigsTest, All) {
  TestEnvironment::exec({TestEnvironment::runfilesPath("test/example_configs_test_setup.sh")});
  EXPECT_EQ(8UL, runConfigTest());
}
