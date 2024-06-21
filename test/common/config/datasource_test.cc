#include "envoy/config/core/v3/base.pb.h"
#include "envoy/config/core/v3/base.pb.validate.h"

#include "source/common/common/cleanup.h"
#include "source/common/common/empty_string.h"
#include "source/common/config/datasource.h"
#include "source/common/http/message_impl.h"
#include "source/common/protobuf/protobuf.h"

#include "test/mocks/event/mocks.h"
#include "test/mocks/init/mocks.h"
#include "test/mocks/thread_local/mocks.h"
#include "test/mocks/upstream/cluster_manager.h"
#include "test/test_common/environment.h"
#include "test/test_common/utility.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace Config {
namespace {
using ::testing::AtLeast;
using ::testing::NiceMock;

class AsyncDataSourceTest : public testing::Test {
protected:
  using AsyncDataSourcePb = envoy::config::core::v3::AsyncDataSource;

  NiceMock<Upstream::MockClusterManager> cm_;
  Init::MockManager init_manager_;
  Init::ExpectableWatcherImpl init_watcher_;
  Init::TargetHandlePtr init_target_handle_;
  Api::ApiPtr api_{Api::createApiForTest()};
  NiceMock<Random::MockRandomGenerator> random_;
  Event::MockDispatcher dispatcher_;
  Event::MockTimer* retry_timer_;
  Event::TimerCb retry_timer_cb_;
  NiceMock<Http::MockAsyncClientRequest> request_{&cm_.thread_local_cluster_.async_client_};

  using AsyncClientSendFunc = std::function<Http::AsyncClient::Request*(
      Http::RequestMessagePtr&, Http::AsyncClient::Callbacks&,
      const Http::AsyncClient::RequestOptions)>;

  void initialize(AsyncClientSendFunc func, int num_retries = 1) {
    retry_timer_ = new Event::MockTimer();
    EXPECT_CALL(init_manager_, add(_)).WillOnce(Invoke([this](const Init::Target& target) {
      init_target_handle_ = target.createHandle("test");
    }));

    EXPECT_CALL(dispatcher_, createTimer_(_)).WillOnce(Invoke([this](Event::TimerCb timer_cb) {
      retry_timer_cb_ = timer_cb;
      return retry_timer_;
    }));

    EXPECT_CALL(*retry_timer_, disableTimer());
    if (!func) {
      return;
    }

    EXPECT_CALL(cm_.thread_local_cluster_, httpAsyncClient())
        .Times(AtLeast(1))
        .WillRepeatedly(ReturnRef(cm_.thread_local_cluster_.async_client_));

    if (num_retries == 1) {
      EXPECT_CALL(cm_.thread_local_cluster_.async_client_, send_(_, _, _))
          .Times(AtLeast(1))
          .WillRepeatedly(Invoke(func));
    } else {
      EXPECT_CALL(cm_.thread_local_cluster_.async_client_, send_(_, _, _))
          .Times(num_retries)
          .WillRepeatedly(Invoke(func));
    }
  }
};

TEST_F(AsyncDataSourceTest, BaseIntervalTest) {
  AsyncDataSourcePb config;

  std::string yaml = R"EOF(
    remote:
      http_uri:
        uri: https://example.com/data
        cluster: cluster_1
        timeout: 1s
      sha256:
        xxx
      retry_policy:
        retry_back_off:
          base_interval: 0.0001s
        num_retries: 3
  )EOF";
  EXPECT_THROW(TestUtility::loadFromYamlAndValidate(yaml, config), EnvoyException);
}

TEST(DataSourceTest, WellKnownEnvironmentVariableTest) {
  envoy::config::core::v3::DataSource config;

  const std::string yaml = R"EOF(
    environment_variable:
      PATH
  )EOF";
  TestUtility::loadFromYamlAndValidate(yaml, config);

  EXPECT_EQ(envoy::config::core::v3::DataSource::SpecifierCase::kEnvironmentVariable,
            config.specifier_case());
  EXPECT_EQ(config.environment_variable(), "PATH");
  Api::ApiPtr api = Api::createApiForTest();
  const auto path_data = DataSource::read(config, false, *api).value();
  EXPECT_FALSE(path_data.empty());
}

TEST(DataSourceTest, MissingEnvironmentVariableTest) {
  envoy::config::core::v3::DataSource config;

  const std::string yaml = R"EOF(
    environment_variable:
      ThisVariableDoesntExist
  )EOF";
  TestUtility::loadFromYamlAndValidate(yaml, config);

  EXPECT_EQ(envoy::config::core::v3::DataSource::SpecifierCase::kEnvironmentVariable,
            config.specifier_case());
  EXPECT_EQ(config.environment_variable(), "ThisVariableDoesntExist");
  Api::ApiPtr api = Api::createApiForTest();
  EXPECT_EQ(DataSource::read(config, false, *api).status().message(),
            "Environment variable doesn't exist: ThisVariableDoesntExist");
  EXPECT_EQ(DataSource::read(config, true, *api).status().message(),
            "Environment variable doesn't exist: ThisVariableDoesntExist");
}

TEST(DataSourceTest, EmptyEnvironmentVariableTest) {
  envoy::config::core::v3::DataSource config;
  TestEnvironment::setEnvVar("ThisVariableIsEmpty", "", 1);
  Envoy::Cleanup cleanup([]() { TestEnvironment::unsetEnvVar("ThisVariableIsEmpty"); });

  const std::string yaml = R"EOF(
    environment_variable:
      ThisVariableIsEmpty
  )EOF";
  TestUtility::loadFromYamlAndValidate(yaml, config);

  EXPECT_EQ(envoy::config::core::v3::DataSource::SpecifierCase::kEnvironmentVariable,
            config.specifier_case());
  EXPECT_EQ(config.environment_variable(), "ThisVariableIsEmpty");
  Api::ApiPtr api = Api::createApiForTest();
#ifdef WIN32
  // Windows doesn't support empty environment variables.
  EXPECT_EQ(DataSource::read(config, false, *api).status().message(),
            "Environment variable doesn't exist: ThisVariableIsEmpty");
  EXPECT_EQ(DataSource::read(config, true, *api).status().message(),
            "Environment variable doesn't exist: ThisVariableIsEmpty");
#else
  EXPECT_EQ(DataSource::read(config, false, *api).status().message(), "DataSource cannot be empty");
  const auto environment_variable = DataSource::read(config, true, *api).value();
  EXPECT_TRUE(environment_variable.empty());
#endif
}

TEST(DataSourceTest, NotExistFileTest) {
  envoy::config::core::v3::DataSource config;
  TestEnvironment::createPath(TestEnvironment::temporaryPath("envoy_test"));
  const std::string filename = TestEnvironment::temporaryPath("envoy_test/not_exist_file");

  const std::string yaml = fmt::format(R"EOF(
    filename: "{}"
  )EOF",
                                       filename);
  TestUtility::loadFromYamlAndValidate(yaml, config);

  EXPECT_EQ(envoy::config::core::v3::DataSource::SpecifierCase::kFilename, config.specifier_case());
  EXPECT_EQ(config.filename(), filename);
  Api::ApiPtr api = Api::createApiForTest();
  EXPECT_EQ(DataSource::read(config, false, *api, 555).status().message(),
            fmt::format("file {} does not exist", filename));
}

TEST(DataSourceTest, EmptyFileTest) {
  envoy::config::core::v3::DataSource config;
  TestEnvironment::createPath(TestEnvironment::temporaryPath("envoy_test"));
  const std::string filename = TestEnvironment::temporaryPath("envoy_test/empty_file");
  {
    std::ofstream file(filename);
    file.close();
  }

  const std::string yaml = fmt::format(R"EOF(
    filename: "{}"
  )EOF",
                                       filename);
  TestUtility::loadFromYamlAndValidate(yaml, config);
  EXPECT_EQ(envoy::config::core::v3::DataSource::SpecifierCase::kFilename, config.specifier_case());
  EXPECT_EQ(config.filename(), filename);

  Api::ApiPtr api = Api::createApiForTest();

  EXPECT_EQ(DataSource::read(config, false, *api, 555).status().message(),
            fmt::format("file {} is empty", filename));

  const auto file_data = DataSource::read(config, true, *api, 555).value();
  EXPECT_TRUE(file_data.empty());
}

TEST(DataSourceProviderTest, NonFileDataSourceTest) {
  envoy::config::core::v3::DataSource config;
  TestEnvironment::createPath(TestEnvironment::temporaryPath("envoy_test"));

  const std::string yaml = fmt::format(R"EOF(
    inline_string: "Hello, world!"
    watched_directory:
      path: "{}"
  )EOF",
                                       TestEnvironment::temporaryPath("envoy_test"));
  TestUtility::loadFromYamlAndValidate(yaml, config);

  EXPECT_EQ(envoy::config::core::v3::DataSource::SpecifierCase::kInlineString,
            config.specifier_case());

  Api::ApiPtr api = Api::createApiForTest();
  Event::DispatcherPtr dispatcher = api->allocateDispatcher("test_thread");
  NiceMock<ThreadLocal::MockInstance> tls;

  auto provider_or_error =
      DataSource::DataSourceProvider::create(config, *dispatcher, tls, *api, false, 0);
  EXPECT_EQ(provider_or_error.value()->data(), "Hello, world!");
}

TEST(DataSourceProviderTest, FileDataSourceButNoWatch) {
  unlink(TestEnvironment::temporaryPath("envoy_test/watcher_target").c_str());
  unlink(TestEnvironment::temporaryPath("envoy_test/watcher_link").c_str());
  unlink(TestEnvironment::temporaryPath("envoy_test/watcher_new_target").c_str());
  unlink(TestEnvironment::temporaryPath("envoy_test/watcher_new_link").c_str());

  envoy::config::core::v3::DataSource config;
  TestEnvironment::createPath(TestEnvironment::temporaryPath("envoy_test"));

  const std::string yaml = fmt::format(R"EOF(
    filename: "{}"
  )EOF",
                                       TestEnvironment::temporaryPath("envoy_test/watcher_link"));
  TestUtility::loadFromYamlAndValidate(yaml, config);

  {
    std::ofstream file(TestEnvironment::temporaryPath("envoy_test/watcher_target"));
    file << "Hello, world!";
    file.close();
  }
  TestEnvironment::createSymlink(TestEnvironment::temporaryPath("envoy_test/watcher_target"),
                                 TestEnvironment::temporaryPath("envoy_test/watcher_link"));
  {
    std::ofstream file(TestEnvironment::temporaryPath("envoy_test/watcher_new_target"));
    file << "Hello, world! Updated!";
    file.close();
  }
  TestEnvironment::createSymlink(TestEnvironment::temporaryPath("envoy_test/watcher_new_target"),
                                 TestEnvironment::temporaryPath("envoy_test/watcher_new_link"));

  EXPECT_EQ(envoy::config::core::v3::DataSource::SpecifierCase::kFilename, config.specifier_case());

  Api::ApiPtr api = Api::createApiForTest();
  Event::DispatcherPtr dispatcher = api->allocateDispatcher("test_thread");
  NiceMock<ThreadLocal::MockInstance> tls;

  auto provider_or_error =
      DataSource::DataSourceProvider::create(config, *dispatcher, tls, *api, false, 0);
  EXPECT_EQ(provider_or_error.value()->data(), "Hello, world!");

  // Update the symlink to point to the new file.
  TestEnvironment::renameFile(TestEnvironment::temporaryPath("envoy_test/watcher_new_link"),
                              TestEnvironment::temporaryPath("envoy_test/watcher_link"));
  // Handle the events if any.
  dispatcher->run(Event::Dispatcher::RunType::NonBlock);

  // The provider should still return the old content.
  EXPECT_EQ(provider_or_error.value()->data(), "Hello, world!");

  // Remove the file.
  unlink(TestEnvironment::temporaryPath("envoy_test/watcher_target").c_str());
  unlink(TestEnvironment::temporaryPath("envoy_test/watcher_link").c_str());
  unlink(TestEnvironment::temporaryPath("envoy_test/watcher_new_target").c_str());
  unlink(TestEnvironment::temporaryPath("envoy_test/watcher_new_link").c_str());
}

TEST(DataSourceProviderTest, FileDataSourceAndWithWatch) {
  unlink(TestEnvironment::temporaryPath("envoy_test/watcher_target").c_str());
  unlink(TestEnvironment::temporaryPath("envoy_test/watcher_link").c_str());
  unlink(TestEnvironment::temporaryPath("envoy_test/watcher_new_target").c_str());
  unlink(TestEnvironment::temporaryPath("envoy_test/watcher_new_link").c_str());

  envoy::config::core::v3::DataSource config;
  TestEnvironment::createPath(TestEnvironment::temporaryPath("envoy_test"));

  const std::string yaml = fmt::format(R"EOF(
    filename: "{}"
    watched_directory:
      path: "{}"
  )EOF",
                                       TestEnvironment::temporaryPath("envoy_test/watcher_link"),
                                       TestEnvironment::temporaryPath("envoy_test"));
  TestUtility::loadFromYamlAndValidate(yaml, config);

  {
    std::ofstream file(TestEnvironment::temporaryPath("envoy_test/watcher_target"));
    file << "Hello, world!";
    file.close();
  }
  TestEnvironment::createSymlink(TestEnvironment::temporaryPath("envoy_test/watcher_target"),
                                 TestEnvironment::temporaryPath("envoy_test/watcher_link"));
  {
    std::ofstream file(TestEnvironment::temporaryPath("envoy_test/watcher_new_target"));
    file << "Hello, world! Updated!";
    file.close();
  }
  TestEnvironment::createSymlink(TestEnvironment::temporaryPath("envoy_test/watcher_new_target"),
                                 TestEnvironment::temporaryPath("envoy_test/watcher_new_link"));

  EXPECT_EQ(envoy::config::core::v3::DataSource::SpecifierCase::kFilename, config.specifier_case());

  Api::ApiPtr api = Api::createApiForTest();
  Event::DispatcherPtr dispatcher = api->allocateDispatcher("test_thread");
  NiceMock<ThreadLocal::MockInstance> tls;

  // Create a provider with watch.
  auto provider_or_error =
      DataSource::DataSourceProvider::create(config, *dispatcher, tls, *api, false, 0);
  EXPECT_EQ(provider_or_error.value()->data(), "Hello, world!");

  // Update the symlink to point to the new file.
  TestEnvironment::renameFile(TestEnvironment::temporaryPath("envoy_test/watcher_new_link"),
                              TestEnvironment::temporaryPath("envoy_test/watcher_link"));
  // Handle the events if any.
  dispatcher->run(Event::Dispatcher::RunType::NonBlock);

  // The provider should return the updated content.
  EXPECT_EQ(provider_or_error.value()->data(), "Hello, world! Updated!");

  // Remove the file.
  unlink(TestEnvironment::temporaryPath("envoy_test/watcher_target").c_str());
  unlink(TestEnvironment::temporaryPath("envoy_test/watcher_link").c_str());
  unlink(TestEnvironment::temporaryPath("envoy_test/watcher_new_target").c_str());
  unlink(TestEnvironment::temporaryPath("envoy_test/watcher_new_link").c_str());
}

TEST(DataSourceProviderTest, FileDataSourceAndWithWatchButUpdateError) {
  unlink(TestEnvironment::temporaryPath("envoy_test/watcher_target").c_str());
  unlink(TestEnvironment::temporaryPath("envoy_test/watcher_link").c_str());
  unlink(TestEnvironment::temporaryPath("envoy_test/watcher_new_target").c_str());
  unlink(TestEnvironment::temporaryPath("envoy_test/watcher_new_link").c_str());

  envoy::config::core::v3::DataSource config;
  TestEnvironment::createPath(TestEnvironment::temporaryPath("envoy_test"));

  const std::string yaml = fmt::format(R"EOF(
    filename: "{}"
    watched_directory:
      path: "{}"
  )EOF",
                                       TestEnvironment::temporaryPath("envoy_test/watcher_link"),
                                       TestEnvironment::temporaryPath("envoy_test"));
  TestUtility::loadFromYamlAndValidate(yaml, config);

  {
    std::ofstream file(TestEnvironment::temporaryPath("envoy_test/watcher_target"));
    file << "Hello, world!";
    file.close();
  }
  TestEnvironment::createSymlink(TestEnvironment::temporaryPath("envoy_test/watcher_target"),
                                 TestEnvironment::temporaryPath("envoy_test/watcher_link"));
  {
    std::ofstream file(TestEnvironment::temporaryPath("envoy_test/watcher_new_target"));
    file << "Hello, world! Updated!";
    file.close();
  }
  TestEnvironment::createSymlink(TestEnvironment::temporaryPath("envoy_test/watcher_new_target"),
                                 TestEnvironment::temporaryPath("envoy_test/watcher_new_link"));

  EXPECT_EQ(envoy::config::core::v3::DataSource::SpecifierCase::kFilename, config.specifier_case());

  Api::ApiPtr api = Api::createApiForTest();
  Event::DispatcherPtr dispatcher = api->allocateDispatcher("test_thread");
  NiceMock<ThreadLocal::MockInstance> tls;

  // Create a provider with watch. The max size is set to 15, so the updated content will be
  // ignored.
  auto provider_or_error =
      DataSource::DataSourceProvider::create(config, *dispatcher, tls, *api, false, 15);
  EXPECT_EQ(provider_or_error.value()->data(), "Hello, world!");

  // Update the symlink to point to the new file.
  TestEnvironment::renameFile(TestEnvironment::temporaryPath("envoy_test/watcher_new_link"),
                              TestEnvironment::temporaryPath("envoy_test/watcher_link"));
  // Handle the events if any.
  dispatcher->run(Event::Dispatcher::RunType::NonBlock);

  // The provider should return the old content because the updated content is ignored.
  EXPECT_EQ(provider_or_error.value()->data(), "Hello, world!");

  // Remove the file.
  unlink(TestEnvironment::temporaryPath("envoy_test/watcher_target").c_str());
  unlink(TestEnvironment::temporaryPath("envoy_test/watcher_link").c_str());
  unlink(TestEnvironment::temporaryPath("envoy_test/watcher_new_target").c_str());
  unlink(TestEnvironment::temporaryPath("envoy_test/watcher_new_link").c_str());
}

} // namespace
} // namespace Config
} // namespace Envoy
