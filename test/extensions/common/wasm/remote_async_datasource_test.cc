#include "test/extensions/common/wasm/remote_async_datasource_base_test.h"

namespace Envoy {
namespace Extensions {
namespace Common {
namespace Wasm {
namespace {

class RemoteAsyncDataSourceTest : public AsyncDataSourceTest {
protected:
  RemoteAsyncDataProviderPtr remote_data_provider_;
};

TEST_F(RemoteAsyncDataSourceTest, LoadRemoteDataSourceNoCluster) {
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
  remote_data_provider_ = std::make_unique<RemoteAsyncDataProvider>(
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

TEST_F(RemoteAsyncDataSourceTest, LoadRemoteDataSourceReturnFailure) {
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
  remote_data_provider_ = std::make_unique<RemoteAsyncDataProvider>(
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

TEST_F(RemoteAsyncDataSourceTest, LoadRemoteDataSourceSuccessWith503) {
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
  remote_data_provider_ = std::make_unique<RemoteAsyncDataProvider>(
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

TEST_F(RemoteAsyncDataSourceTest, LoadRemoteDataSourceSuccessWithEmptyBody) {
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
  remote_data_provider_ = std::make_unique<RemoteAsyncDataProvider>(
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

TEST_F(RemoteAsyncDataSourceTest, LoadRemoteDataSourceSuccessIncorrectSha256) {
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
  remote_data_provider_ = std::make_unique<RemoteAsyncDataProvider>(
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

TEST_F(RemoteAsyncDataSourceTest, LoadRemoteDataSourceSuccess) {
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
  remote_data_provider_ = std::make_unique<RemoteAsyncDataProvider>(
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

TEST_F(RemoteAsyncDataSourceTest, LoadRemoteDataSourceDoNotAllowEmpty) {
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
  remote_data_provider_ = std::make_unique<RemoteAsyncDataProvider>(
      cm_, init_manager_, config.remote(), dispatcher_, random_, false,
      [&](const std::string& data) { async_data = data; });

  EXPECT_CALL(init_watcher_, ready());
  EXPECT_CALL(*retry_timer_, enableTimer(_, _))
      .WillOnce(Invoke(
          [&](const std::chrono::milliseconds&, const ScopeTrackedObject*) { retry_timer_cb_(); }));
  init_target_handle_->initialize(init_watcher_);

  EXPECT_EQ(async_data, "non-empty");
}

TEST_F(RemoteAsyncDataSourceTest, DatasourceReleasedBeforeFetchingData) {
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

    remote_data_provider_ = std::make_unique<RemoteAsyncDataProvider>(
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

TEST_F(RemoteAsyncDataSourceTest, LoadRemoteDataSourceWithRetry) {
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
  remote_data_provider_ = std::make_unique<RemoteAsyncDataProvider>(
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

TEST_F(RemoteAsyncDataSourceTest, BaseIntervalGreaterThanMaxInterval) {
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

  EXPECT_THROW_WITH_MESSAGE(
      std::make_unique<RemoteAsyncDataProvider>(cm_, init_manager_, config.remote(), dispatcher_,
                                                random_, true, [&](const std::string&) {}),
      EnvoyException, "max_interval must be greater than or equal to the base_interval");
}

TEST_F(RemoteAsyncDataSourceTest, BaseIntervalTest) {
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
} // namespace Wasm
} // namespace Common
} // namespace Extensions
} // namespace Envoy
