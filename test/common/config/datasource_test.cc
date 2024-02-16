#include "envoy/config/core/v3/base.pb.h"
#include "envoy/config/core/v3/base.pb.validate.h"

#include "source/common/common/cleanup.h"
#include "source/common/common/empty_string.h"
#include "source/common/config/datasource.h"
#include "source/common/http/message_impl.h"
#include "source/common/protobuf/protobuf.h"

#include "test/mocks/event/mocks.h"
#include "test/mocks/init/mocks.h"
#include "test/mocks/upstream/cluster_manager.h"
#include "test/test_common/environment.h"
#include "test/test_common/utility.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace Config {
namespace {
using ::testing::AtLeast;
using ::testing::NiceMock;
using ::testing::Return;

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

} // namespace
} // namespace Config
} // namespace Envoy
