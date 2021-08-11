#include "envoy/config/core/v3/base.pb.h"
#include "envoy/config/core/v3/base.pb.validate.h"

#include "source/common/common/empty_string.h"
#include "source/common/config/datasource.h"
#include "source/common/http/message_impl.h"
#include "source/common/protobuf/protobuf.h"

#include "test/mocks/event/mocks.h"
#include "test/mocks/init/mocks.h"
#include "test/mocks/upstream/cluster_manager.h"
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

  Config::DataSource::LocalAsyncDataProviderPtr local_data_provider_;
  Config::DataSource::RemoteAsyncDataProviderPtr remote_data_provider_;

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

TEST_F(AsyncDataSourceTest, LoadLocalDataSource) {
  AsyncDataSourcePb config;

  std::string yaml = R"EOF(
    local:
      inline_string:
        xxxxxx
  )EOF";
  TestUtility::loadFromYamlAndValidate(yaml, config);
  EXPECT_TRUE(config.has_local());

  std::string async_data;

  EXPECT_CALL(init_manager_, add(_)).WillOnce(Invoke([this](const Init::Target& target) {
    init_target_handle_ = target.createHandle("test");
  }));

  local_data_provider_ = std::make_unique<Config::DataSource::LocalAsyncDataProvider>(
      init_manager_, config.local(), true, *api_, [&](const std::string& data) {
        EXPECT_EQ(init_manager_.state(), Init::Manager::State::Initializing);
        EXPECT_EQ(data, "xxxxxx");
        async_data = data;
      });

  EXPECT_CALL(init_manager_, state()).WillOnce(Return(Init::Manager::State::Initializing));
  EXPECT_CALL(init_watcher_, ready());

  init_target_handle_->initialize(init_watcher_);
  EXPECT_EQ(async_data, "xxxxxx");
}

TEST_F(AsyncDataSourceTest, LoadLocalEmptyDataSource) {
  AsyncDataSourcePb config;

  std::string yaml = R"EOF(
    local:
      inline_string: ""
  )EOF";
  TestUtility::loadFromYamlAndValidate(yaml, config);
  EXPECT_TRUE(config.has_local());

  std::string async_data;

  EXPECT_CALL(init_manager_, add(_)).WillOnce(Invoke([this](const Init::Target& target) {
    init_target_handle_ = target.createHandle("test");
  }));

  local_data_provider_ = std::make_unique<Config::DataSource::LocalAsyncDataProvider>(
      init_manager_, config.local(), true, *api_, [&](const std::string& data) {
        EXPECT_EQ(init_manager_.state(), Init::Manager::State::Initializing);
        EXPECT_EQ(data, "");
        async_data = data;
      });

  EXPECT_CALL(init_manager_, state()).WillOnce(Return(Init::Manager::State::Initializing));
  EXPECT_CALL(init_watcher_, ready());

  init_target_handle_->initialize(init_watcher_);
  EXPECT_EQ(async_data, "");
}

TEST_F(AsyncDataSourceTest, LoadRemoteDataSourceNoCluster) {
  AsyncDataSourcePb config;

  std::string yaml = R"EOF(
    remote:
      http_uri:
        uri: https://example.com/data
        cluster: cluster_1
        timeout: 1s
      sha256:
        xxxxxx
  )EOF";
  TestUtility::loadFromYamlAndValidate(yaml, config);
  EXPECT_TRUE(config.has_remote());

  initialize(nullptr);

  std::string async_data = "non-empty";
  remote_data_provider_ = std::make_unique<Config::DataSource::RemoteAsyncDataProvider>(
      cm_, init_manager_, config.remote(), dispatcher_, random_, true,
      [&](const std::string& data) {
        EXPECT_EQ(init_manager_.state(), Init::Manager::State::Initializing);
        EXPECT_EQ(data, EMPTY_STRING);
        async_data = data;
      });

  EXPECT_CALL(init_manager_, state()).WillOnce(Return(Init::Manager::State::Initializing));
  EXPECT_CALL(init_watcher_, ready());
  EXPECT_CALL(*retry_timer_, enableTimer(_, _))
      .WillOnce(Invoke(
          [&](const std::chrono::milliseconds&, const ScopeTrackedObject*) { retry_timer_cb_(); }));
  init_target_handle_->initialize(init_watcher_);

  EXPECT_EQ(async_data, EMPTY_STRING);
}

TEST_F(AsyncDataSourceTest, LoadRemoteDataSourceReturnFailure) {
  AsyncDataSourcePb config;

  std::string yaml = R"EOF(
    remote:
      http_uri:
        uri: https://example.com/data
        cluster: cluster_1
        timeout: 1s
      sha256:
        xxxxxx
  )EOF";
  TestUtility::loadFromYamlAndValidate(yaml, config);
  EXPECT_TRUE(config.has_remote());

  cm_.initializeThreadLocalClusters({"cluster_1"});
  initialize([&](Http::RequestMessagePtr&, Http::AsyncClient::Callbacks& callbacks,
                 const Http::AsyncClient::RequestOptions&) -> Http::AsyncClient::Request* {
    callbacks.onFailure(request_, Envoy::Http::AsyncClient::FailureReason::Reset);
    return nullptr;
  });

  std::string async_data = "non-empty";
  remote_data_provider_ = std::make_unique<Config::DataSource::RemoteAsyncDataProvider>(
      cm_, init_manager_, config.remote(), dispatcher_, random_, true,
      [&](const std::string& data) {
        EXPECT_EQ(init_manager_.state(), Init::Manager::State::Initializing);
        EXPECT_EQ(data, EMPTY_STRING);
        async_data = data;
      });

  EXPECT_CALL(init_manager_, state()).WillOnce(Return(Init::Manager::State::Initializing));
  EXPECT_CALL(init_watcher_, ready());
  EXPECT_CALL(*retry_timer_, enableTimer(_, _))
      .WillOnce(Invoke(
          [&](const std::chrono::milliseconds&, const ScopeTrackedObject*) { retry_timer_cb_(); }));
  init_target_handle_->initialize(init_watcher_);

  EXPECT_EQ(async_data, EMPTY_STRING);
}

TEST_F(AsyncDataSourceTest, LoadRemoteDataSourceSuccessWith503) {
  AsyncDataSourcePb config;

  std::string yaml = R"EOF(
    remote:
      http_uri:
        uri: https://example.com/data
        cluster: cluster_1
        timeout: 1s
      sha256:
        xxxxxx
  )EOF";
  TestUtility::loadFromYamlAndValidate(yaml, config);
  EXPECT_TRUE(config.has_remote());

  cm_.initializeThreadLocalClusters({"cluster_1"});
  initialize([&](Http::RequestMessagePtr&, Http::AsyncClient::Callbacks& callbacks,
                 const Http::AsyncClient::RequestOptions&) -> Http::AsyncClient::Request* {
    callbacks.onSuccess(
        request_, Http::ResponseMessagePtr{new Http::ResponseMessageImpl(Http::ResponseHeaderMapPtr{
                      new Http::TestResponseHeaderMapImpl{{":status", "503"}}})});
    return nullptr;
  });

  std::string async_data = "non-empty";
  remote_data_provider_ = std::make_unique<Config::DataSource::RemoteAsyncDataProvider>(
      cm_, init_manager_, config.remote(), dispatcher_, random_, true,
      [&](const std::string& data) {
        EXPECT_EQ(init_manager_.state(), Init::Manager::State::Initializing);
        EXPECT_EQ(data, EMPTY_STRING);
        async_data = data;
      });

  EXPECT_CALL(init_manager_, state()).WillOnce(Return(Init::Manager::State::Initializing));
  EXPECT_CALL(init_watcher_, ready());
  EXPECT_CALL(*retry_timer_, enableTimer(_, _))
      .WillOnce(Invoke(
          [&](const std::chrono::milliseconds&, const ScopeTrackedObject*) { retry_timer_cb_(); }));
  init_target_handle_->initialize(init_watcher_);

  EXPECT_EQ(async_data, EMPTY_STRING);
}

TEST_F(AsyncDataSourceTest, LoadRemoteDataSourceSuccessWithEmptyBody) {
  AsyncDataSourcePb config;

  std::string yaml = R"EOF(
    remote:
      http_uri:
        uri: https://example.com/data
        cluster: cluster_1
        timeout: 1s
      sha256:
        xxxxxx
  )EOF";
  TestUtility::loadFromYamlAndValidate(yaml, config);
  EXPECT_TRUE(config.has_remote());

  cm_.initializeThreadLocalClusters({"cluster_1"});
  initialize([&](Http::RequestMessagePtr&, Http::AsyncClient::Callbacks& callbacks,
                 const Http::AsyncClient::RequestOptions&) -> Http::AsyncClient::Request* {
    callbacks.onSuccess(
        request_, Http::ResponseMessagePtr{new Http::ResponseMessageImpl(Http::ResponseHeaderMapPtr{
                      new Http::TestResponseHeaderMapImpl{{":status", "200"}}})});
    return nullptr;
  });

  std::string async_data = "non-empty";
  remote_data_provider_ = std::make_unique<Config::DataSource::RemoteAsyncDataProvider>(
      cm_, init_manager_, config.remote(), dispatcher_, random_, true,
      [&](const std::string& data) {
        EXPECT_EQ(init_manager_.state(), Init::Manager::State::Initializing);
        EXPECT_EQ(data, EMPTY_STRING);
        async_data = data;
      });

  EXPECT_CALL(init_manager_, state()).WillOnce(Return(Init::Manager::State::Initializing));
  EXPECT_CALL(init_watcher_, ready());
  EXPECT_CALL(*retry_timer_, enableTimer(_, _))
      .WillOnce(Invoke(
          [&](const std::chrono::milliseconds&, const ScopeTrackedObject*) { retry_timer_cb_(); }));
  init_target_handle_->initialize(init_watcher_);

  EXPECT_EQ(async_data, EMPTY_STRING);
}

TEST_F(AsyncDataSourceTest, LoadRemoteDataSourceSuccessIncorrectSha256) {
  AsyncDataSourcePb config;

  std::string yaml = R"EOF(
    remote:
      http_uri:
        uri: https://example.com/data
        cluster: cluster_1
        timeout: 1s
      sha256:
        xxxxxx
  )EOF";
  TestUtility::loadFromYamlAndValidate(yaml, config);
  EXPECT_TRUE(config.has_remote());

  const std::string body = "hello world";

  cm_.initializeThreadLocalClusters({"cluster_1"});
  initialize([&](Http::RequestMessagePtr&, Http::AsyncClient::Callbacks& callbacks,
                 const Http::AsyncClient::RequestOptions&) -> Http::AsyncClient::Request* {
    Http::ResponseMessagePtr response(new Http::ResponseMessageImpl(
        Http::ResponseHeaderMapPtr{new Http::TestResponseHeaderMapImpl{{":status", "200"}}}));
    response->body().add(body);

    callbacks.onSuccess(request_, std::move(response));
    return nullptr;
  });

  std::string async_data = "non-empty";
  remote_data_provider_ = std::make_unique<Config::DataSource::RemoteAsyncDataProvider>(
      cm_, init_manager_, config.remote(), dispatcher_, random_, true,
      [&](const std::string& data) {
        EXPECT_EQ(init_manager_.state(), Init::Manager::State::Initializing);
        EXPECT_EQ(data, EMPTY_STRING);
        async_data = data;
      });

  EXPECT_CALL(init_manager_, state()).WillOnce(Return(Init::Manager::State::Initializing));
  EXPECT_CALL(init_watcher_, ready());
  EXPECT_CALL(*retry_timer_, enableTimer(_, _))
      .WillOnce(Invoke(
          [&](const std::chrono::milliseconds&, const ScopeTrackedObject*) { retry_timer_cb_(); }));
  init_target_handle_->initialize(init_watcher_);

  EXPECT_EQ(async_data, EMPTY_STRING);
}

TEST_F(AsyncDataSourceTest, LoadRemoteDataSourceSuccess) {
  AsyncDataSourcePb config;

  std::string yaml = R"EOF(
    remote:
      http_uri:
        uri: https://example.com/data
        cluster: cluster_1
        timeout: 1s
      sha256:
        b94d27b9934d3e08a52e52d7da7dabfac484efe37a5380ee9088f7ace2efcde9
  )EOF";
  TestUtility::loadFromYamlAndValidate(yaml, config);
  EXPECT_TRUE(config.has_remote());

  cm_.initializeThreadLocalClusters({"cluster_1"});
  const std::string body = "hello world";
  initialize([&](Http::RequestMessagePtr&, Http::AsyncClient::Callbacks& callbacks,
                 const Http::AsyncClient::RequestOptions&) -> Http::AsyncClient::Request* {
    Http::ResponseMessagePtr response(new Http::ResponseMessageImpl(
        Http::ResponseHeaderMapPtr{new Http::TestResponseHeaderMapImpl{{":status", "200"}}}));
    response->body().add(body);

    callbacks.onSuccess(request_, std::move(response));
    return nullptr;
  });

  std::string async_data = "non-empty";
  remote_data_provider_ = std::make_unique<Config::DataSource::RemoteAsyncDataProvider>(
      cm_, init_manager_, config.remote(), dispatcher_, random_, true,
      [&](const std::string& data) {
        EXPECT_EQ(init_manager_.state(), Init::Manager::State::Initializing);
        EXPECT_EQ(data, body);
        async_data = data;
      });

  EXPECT_CALL(init_manager_, state()).WillOnce(Return(Init::Manager::State::Initializing));
  EXPECT_CALL(init_watcher_, ready());
  init_target_handle_->initialize(init_watcher_);

  EXPECT_EQ(async_data, body);
}

TEST_F(AsyncDataSourceTest, LoadRemoteDataSourceDoNotAllowEmpty) {
  AsyncDataSourcePb config;

  std::string yaml = R"EOF(
    remote:
      http_uri:
        uri: https://example.com/data
        cluster: cluster_1
        timeout: 1s
      sha256:
        xxxxxx
  )EOF";
  TestUtility::loadFromYamlAndValidate(yaml, config);
  EXPECT_TRUE(config.has_remote());

  cm_.initializeThreadLocalClusters({"cluster_1"});
  initialize([&](Http::RequestMessagePtr&, Http::AsyncClient::Callbacks& callbacks,
                 const Http::AsyncClient::RequestOptions&) -> Http::AsyncClient::Request* {
    callbacks.onSuccess(
        request_, Http::ResponseMessagePtr{new Http::ResponseMessageImpl(Http::ResponseHeaderMapPtr{
                      new Http::TestResponseHeaderMapImpl{{":status", "503"}}})});
    return nullptr;
  });

  std::string async_data = "non-empty";
  remote_data_provider_ = std::make_unique<Config::DataSource::RemoteAsyncDataProvider>(
      cm_, init_manager_, config.remote(), dispatcher_, random_, false,
      [&](const std::string& data) { async_data = data; });

  EXPECT_CALL(init_watcher_, ready());
  EXPECT_CALL(*retry_timer_, enableTimer(_, _))
      .WillOnce(Invoke(
          [&](const std::chrono::milliseconds&, const ScopeTrackedObject*) { retry_timer_cb_(); }));
  init_target_handle_->initialize(init_watcher_);

  EXPECT_EQ(async_data, "non-empty");
}

TEST_F(AsyncDataSourceTest, DatasourceReleasedBeforeFetchingData) {
  const std::string body = "hello world";
  std::string async_data = "non-empty";

  {
    AsyncDataSourcePb config;

    std::string yaml = R"EOF(
    remote:
      http_uri:
        uri: https://example.com/data
        cluster: cluster_1
        timeout: 1s
      sha256:
        b94d27b9934d3e08a52e52d7da7dabfac484efe37a5380ee9088f7ace2efcde9
  )EOF";
    TestUtility::loadFromYamlAndValidate(yaml, config);
    EXPECT_TRUE(config.has_remote());

    cm_.initializeThreadLocalClusters({"cluster_1"});
    initialize([&](Http::RequestMessagePtr&, Http::AsyncClient::Callbacks& callbacks,
                   const Http::AsyncClient::RequestOptions&) -> Http::AsyncClient::Request* {
      Http::ResponseMessagePtr response(new Http::ResponseMessageImpl(
          Http::ResponseHeaderMapPtr{new Http::TestResponseHeaderMapImpl{{":status", "200"}}}));
      response->body().add(body);

      callbacks.onSuccess(request_, std::move(response));
      return nullptr;
    });

    remote_data_provider_ = std::make_unique<Config::DataSource::RemoteAsyncDataProvider>(
        cm_, init_manager_, config.remote(), dispatcher_, random_, true,
        [&](const std::string& data) {
          EXPECT_EQ(init_manager_.state(), Init::Manager::State::Initializing);
          EXPECT_EQ(data, body);
          async_data = data;
        });
  }

  EXPECT_CALL(init_manager_, state()).WillOnce(Return(Init::Manager::State::Initializing));
  EXPECT_CALL(init_watcher_, ready());
  init_target_handle_->initialize(init_watcher_);
  EXPECT_EQ(async_data, body);
}

TEST_F(AsyncDataSourceTest, LoadRemoteDataSourceWithRetry) {
  AsyncDataSourcePb config;

  std::string yaml = R"EOF(
    remote:
      http_uri:
        uri: https://example.com/data
        cluster: cluster_1
        timeout: 1s
      sha256:
        b94d27b9934d3e08a52e52d7da7dabfac484efe37a5380ee9088f7ace2efcde9
      retry_policy:
        retry_back_off:
          base_interval: 1s
        num_retries: 3
  )EOF";
  TestUtility::loadFromYamlAndValidate(yaml, config);
  EXPECT_TRUE(config.has_remote());

  cm_.initializeThreadLocalClusters({"cluster_1"});
  const std::string body = "hello world";
  int num_retries = 3;

  initialize(
      [&](Http::RequestMessagePtr&, Http::AsyncClient::Callbacks& callbacks,
          const Http::AsyncClient::RequestOptions&) -> Http::AsyncClient::Request* {
        callbacks.onSuccess(
            request_,
            Http::ResponseMessagePtr{new Http::ResponseMessageImpl(Http::ResponseHeaderMapPtr{
                new Http::TestResponseHeaderMapImpl{{":status", "503"}}})});
        return nullptr;
      },
      num_retries);

  std::string async_data = "non-empty";
  remote_data_provider_ = std::make_unique<Config::DataSource::RemoteAsyncDataProvider>(
      cm_, init_manager_, config.remote(), dispatcher_, random_, true,
      [&](const std::string& data) {
        EXPECT_EQ(init_manager_.state(), Init::Manager::State::Initializing);
        EXPECT_EQ(data, body);
        async_data = data;
      });

  EXPECT_CALL(init_manager_, state()).WillOnce(Return(Init::Manager::State::Initializing));
  EXPECT_CALL(init_watcher_, ready());
  EXPECT_CALL(*retry_timer_, enableTimer(_, _))
      .WillRepeatedly(Invoke([&](const std::chrono::milliseconds&, const ScopeTrackedObject*) {
        if (--num_retries == 0) {
          EXPECT_CALL(cm_.thread_local_cluster_.async_client_, send_(_, _, _))
              .WillOnce(Invoke(
                  [&](Http::RequestMessagePtr&, Http::AsyncClient::Callbacks& callbacks,
                      const Http::AsyncClient::RequestOptions&) -> Http::AsyncClient::Request* {
                    Http::ResponseMessagePtr response(
                        new Http::ResponseMessageImpl(Http::ResponseHeaderMapPtr{
                            new Http::TestResponseHeaderMapImpl{{":status", "200"}}}));
                    response->body().add(body);

                    callbacks.onSuccess(request_, std::move(response));
                    return nullptr;
                  }));
        }

        retry_timer_cb_();
      }));
  init_target_handle_->initialize(init_watcher_);

  EXPECT_EQ(async_data, body);
}

TEST_F(AsyncDataSourceTest, BaseIntervalGreaterThanMaxInterval) {
  AsyncDataSourcePb config;

  std::string yaml = R"EOF(
    remote:
      http_uri:
        uri: https://example.com/data
        cluster: cluster_1
        timeout: 1s
      sha256:
        b94d27b9934d3e08a52e52d7da7dabfac484efe37a5380ee9088f7ace2efcde9
      retry_policy:
        retry_back_off:
          base_interval: 10s
          max_interval: 1s
        num_retries: 3
  )EOF";
  TestUtility::loadFromYamlAndValidate(yaml, config);
  EXPECT_TRUE(config.has_remote());

  EXPECT_THROW_WITH_MESSAGE(std::make_unique<Config::DataSource::RemoteAsyncDataProvider>(
                                cm_, init_manager_, config.remote(), dispatcher_, random_, true,
                                [&](const std::string&) {}),
                            EnvoyException,
                            "max_interval must be greater than or equal to the base_interval");
}

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

} // namespace
} // namespace Config
} // namespace Envoy
