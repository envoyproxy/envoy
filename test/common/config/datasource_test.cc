#include "envoy/config/core/v3/base.pb.h"

#include "common/common/empty_string.h"
#include "common/config/datasource.h"
#include "common/protobuf/protobuf.h"

#include "test/mocks/server/mocks.h"
#include "test/mocks/upstream/mocks.h"
#include "test/test_common/utility.h"

#include "gtest/gtest.h"

using testing::NiceMock;
using testing::Return;

namespace Envoy {
namespace Config {
namespace {

class AsyncDataSourceTest : public testing::Test {
protected:
  AsyncDataSourceTest() : api_(Api::createApiForTest()) {}

  using AsyncDataSourcePb = envoy::config::core::v3::AsyncDataSource;

  NiceMock<Upstream::MockClusterManager> cm_;
  Init::MockManager init_manager_;
  Init::ExpectableWatcherImpl init_watcher_;
  Init::TargetHandlePtr init_target_handle_;
  Api::ApiPtr api_;
};

TEST_F(AsyncDataSourceTest, LoadLocalDataSource) {
  AsyncDataSourcePb config;

  std::string yaml = R"EOF(
    local:
      inline_string:
        xxxxxx
  )EOF";
  TestUtility::loadFromYaml(yaml, config);
  EXPECT_TRUE(config.has_local());

  std::string async_data;

  EXPECT_CALL(init_manager_, add(_)).WillOnce(Invoke([this](const Init::Target& target) {
    init_target_handle_ = target.createHandle("test");
  }));

  auto provider = std::make_unique<Config::DataSource::LocalAsyncDataProvider>(
      init_manager_, config.local(), true, *api_, [&](const std::string& data) {
        EXPECT_EQ(init_manager_.state(), Init::Manager::State::Initializing);
        EXPECT_EQ(data, "xxxxxx");
        async_data = data;
      });

  EXPECT_CALL(init_manager_, state()).WillOnce(Return(Init::Manager::State::Initializing));
  EXPECT_CALL(init_watcher_, ready());

  init_target_handle_->initialize(init_watcher_);

  EXPECT_EQ(async_data, "xxxxxx");
  EXPECT_NE(nullptr, provider.get());
}

TEST_F(AsyncDataSourceTest, LoadRemoteDataSourceReturnFailure) {
  AsyncDataSourcePb config;

  std::string yaml = R"EOF(
    remote:
      http_uri:
        uri: https://example.com/data
        cluster: cluster_1
      sha256:
        xxxxxx
  )EOF";
  TestUtility::loadFromYaml(yaml, config);
  EXPECT_TRUE(config.has_remote());

  EXPECT_CALL(cm_, httpAsyncClientForCluster("cluster_1")).WillOnce(ReturnRef(cm_.async_client_));
  EXPECT_CALL(cm_.async_client_, send_(_, _, _))
      .WillOnce(
          Invoke([&](Http::RequestMessagePtr&, Http::AsyncClient::Callbacks& callbacks,
                     const Http::AsyncClient::RequestOptions&) -> Http::AsyncClient::Request* {
            callbacks.onFailure(Envoy::Http::AsyncClient::FailureReason::Reset);
            return nullptr;
          }));

  EXPECT_CALL(init_manager_, add(_)).WillOnce(Invoke([this](const Init::Target& target) {
    init_target_handle_ = target.createHandle("test");
  }));

  std::string async_data = "non-empty";
  auto provider = std::make_unique<Config::DataSource::RemoteAsyncDataProvider>(
      cm_, init_manager_, config.remote(), true, [&](const std::string& data) {
        EXPECT_EQ(init_manager_.state(), Init::Manager::State::Initializing);
        EXPECT_EQ(data, EMPTY_STRING);
        async_data = data;
      });

  EXPECT_CALL(init_manager_, state()).WillOnce(Return(Init::Manager::State::Initializing));
  EXPECT_CALL(init_watcher_, ready());

  init_target_handle_->initialize(init_watcher_);

  EXPECT_EQ(async_data, EMPTY_STRING);
  EXPECT_NE(nullptr, provider.get());
}

TEST_F(AsyncDataSourceTest, LoadRemoteDataSourceSuccessWith503) {
  AsyncDataSourcePb config;

  std::string yaml = R"EOF(
    remote:
      http_uri:
        uri: https://example.com/data
        cluster: cluster_1
      sha256:
        xxxxxx
  )EOF";
  TestUtility::loadFromYaml(yaml, config);
  EXPECT_TRUE(config.has_remote());

  EXPECT_CALL(cm_, httpAsyncClientForCluster("cluster_1")).WillOnce(ReturnRef(cm_.async_client_));
  EXPECT_CALL(cm_.async_client_, send_(_, _, _))
      .WillOnce(
          Invoke([&](Http::RequestMessagePtr&, Http::AsyncClient::Callbacks& callbacks,
                     const Http::AsyncClient::RequestOptions&) -> Http::AsyncClient::Request* {
            callbacks.onSuccess(
                Http::ResponseMessagePtr{new Http::ResponseMessageImpl(Http::ResponseHeaderMapPtr{
                    new Http::TestResponseHeaderMapImpl{{":status", "503"}}})});
            return nullptr;
          }));

  EXPECT_CALL(init_manager_, add(_)).WillOnce(Invoke([this](const Init::Target& target) {
    init_target_handle_ = target.createHandle("test");
  }));

  std::string async_data = "non-empty";
  auto provider = std::make_unique<Config::DataSource::RemoteAsyncDataProvider>(
      cm_, init_manager_, config.remote(), true, [&](const std::string& data) {
        EXPECT_EQ(init_manager_.state(), Init::Manager::State::Initializing);
        EXPECT_EQ(data, EMPTY_STRING);
        async_data = data;
      });

  EXPECT_CALL(init_manager_, state()).WillOnce(Return(Init::Manager::State::Initializing));
  EXPECT_CALL(init_watcher_, ready());

  init_target_handle_->initialize(init_watcher_);
  EXPECT_EQ(async_data, EMPTY_STRING);
  EXPECT_NE(nullptr, provider.get());
}

TEST_F(AsyncDataSourceTest, LoadRemoteDataSourceSuccessWithEmptyBody) {
  AsyncDataSourcePb config;

  std::string yaml = R"EOF(
    remote:
      http_uri:
        uri: https://example.com/data
        cluster: cluster_1
      sha256:
        xxxxxx
  )EOF";
  TestUtility::loadFromYaml(yaml, config);
  EXPECT_TRUE(config.has_remote());

  EXPECT_CALL(cm_, httpAsyncClientForCluster("cluster_1")).WillOnce(ReturnRef(cm_.async_client_));
  EXPECT_CALL(cm_.async_client_, send_(_, _, _))
      .WillOnce(
          Invoke([&](Http::RequestMessagePtr&, Http::AsyncClient::Callbacks& callbacks,
                     const Http::AsyncClient::RequestOptions&) -> Http::AsyncClient::Request* {
            callbacks.onSuccess(
                Http::ResponseMessagePtr{new Http::ResponseMessageImpl(Http::ResponseHeaderMapPtr{
                    new Http::TestResponseHeaderMapImpl{{":status", "200"}}})});
            return nullptr;
          }));

  EXPECT_CALL(init_manager_, add(_)).WillOnce(Invoke([this](const Init::Target& target) {
    init_target_handle_ = target.createHandle("test");
  }));

  std::string async_data = "non-empty";
  auto provider = std::make_unique<Config::DataSource::RemoteAsyncDataProvider>(
      cm_, init_manager_, config.remote(), true, [&](const std::string& data) {
        EXPECT_EQ(init_manager_.state(), Init::Manager::State::Initializing);
        EXPECT_EQ(data, EMPTY_STRING);
        async_data = data;
      });

  EXPECT_CALL(init_manager_, state()).WillOnce(Return(Init::Manager::State::Initializing));
  EXPECT_CALL(init_watcher_, ready());

  init_target_handle_->initialize(init_watcher_);

  EXPECT_EQ(async_data, EMPTY_STRING);
  EXPECT_NE(nullptr, provider.get());
}

TEST_F(AsyncDataSourceTest, LoadRemoteDataSourceSuccessIncorrectSha256) {
  AsyncDataSourcePb config;

  std::string yaml = R"EOF(
    remote:
      http_uri:
        uri: https://example.com/data
        cluster: cluster_1
      sha256:
        xxxxxx
  )EOF";
  TestUtility::loadFromYaml(yaml, config);
  EXPECT_TRUE(config.has_remote());

  const std::string body = "hello world";

  EXPECT_CALL(cm_, httpAsyncClientForCluster("cluster_1")).WillOnce(ReturnRef(cm_.async_client_));
  EXPECT_CALL(cm_.async_client_, send_(_, _, _))
      .WillOnce(
          Invoke([&](Http::RequestMessagePtr&, Http::AsyncClient::Callbacks& callbacks,
                     const Http::AsyncClient::RequestOptions&) -> Http::AsyncClient::Request* {
            Http::ResponseMessagePtr response(
                new Http::ResponseMessageImpl(Http::ResponseHeaderMapPtr{
                    new Http::TestResponseHeaderMapImpl{{":status", "200"}}}));
            response->body() = std::make_unique<Buffer::OwnedImpl>(body);

            callbacks.onSuccess(std::move(response));
            return nullptr;
          }));

  EXPECT_CALL(init_manager_, add(_)).WillOnce(Invoke([this](const Init::Target& target) {
    init_target_handle_ = target.createHandle("test");
  }));

  std::string async_data = "non-empty";
  auto provider = std::make_unique<Config::DataSource::RemoteAsyncDataProvider>(
      cm_, init_manager_, config.remote(), true, [&](const std::string& data) {
        EXPECT_EQ(init_manager_.state(), Init::Manager::State::Initializing);
        EXPECT_EQ(data, EMPTY_STRING);
        async_data = data;
      });

  EXPECT_CALL(init_manager_, state()).WillOnce(Return(Init::Manager::State::Initializing));
  EXPECT_CALL(init_watcher_, ready());

  init_target_handle_->initialize(init_watcher_);
  EXPECT_EQ(async_data, EMPTY_STRING);
  EXPECT_NE(nullptr, provider.get());
}

TEST_F(AsyncDataSourceTest, LoadRemoteDataSourceSuccess) {
  AsyncDataSourcePb config;

  std::string yaml = R"EOF(
    remote:
      http_uri:
        uri: https://example.com/data
        cluster: cluster_1
      sha256:
        b94d27b9934d3e08a52e52d7da7dabfac484efe37a5380ee9088f7ace2efcde9
  )EOF";
  TestUtility::loadFromYaml(yaml, config);
  EXPECT_TRUE(config.has_remote());

  const std::string body = "hello world";

  EXPECT_CALL(cm_, httpAsyncClientForCluster("cluster_1")).WillOnce(ReturnRef(cm_.async_client_));
  EXPECT_CALL(cm_.async_client_, send_(_, _, _))
      .WillOnce(
          Invoke([&](Http::RequestMessagePtr&, Http::AsyncClient::Callbacks& callbacks,
                     const Http::AsyncClient::RequestOptions&) -> Http::AsyncClient::Request* {
            Http::ResponseMessagePtr response(
                new Http::ResponseMessageImpl(Http::ResponseHeaderMapPtr{
                    new Http::TestResponseHeaderMapImpl{{":status", "200"}}}));
            response->body() = std::make_unique<Buffer::OwnedImpl>(body);

            callbacks.onSuccess(std::move(response));
            return nullptr;
          }));

  EXPECT_CALL(init_manager_, add(_)).WillOnce(Invoke([this](const Init::Target& target) {
    init_target_handle_ = target.createHandle("test");
  }));

  std::string async_data = "non-empty";
  auto provider = std::make_unique<Config::DataSource::RemoteAsyncDataProvider>(
      cm_, init_manager_, config.remote(), true, [&](const std::string& data) {
        EXPECT_EQ(init_manager_.state(), Init::Manager::State::Initializing);
        EXPECT_EQ(data, body);
        async_data = data;
      });

  EXPECT_CALL(init_manager_, state()).WillOnce(Return(Init::Manager::State::Initializing));
  EXPECT_CALL(init_watcher_, ready());

  init_target_handle_->initialize(init_watcher_);
  EXPECT_EQ(async_data, body);
  EXPECT_NE(nullptr, provider.get());
}

TEST_F(AsyncDataSourceTest, LoadRemoteDataSourceExpectNetworkFailure) {
  AsyncDataSourcePb config;

  std::string yaml = R"EOF(
    remote:
      http_uri:
        uri: https://example.com/data
        cluster: cluster_1
      sha256:
        xxxxxx
  )EOF";
  TestUtility::loadFromYaml(yaml, config);
  EXPECT_TRUE(config.has_remote());

  EXPECT_CALL(cm_, httpAsyncClientForCluster("cluster_1")).WillOnce(ReturnRef(cm_.async_client_));
  EXPECT_CALL(cm_.async_client_, send_(_, _, _))
      .WillOnce(
          Invoke([&](Http::RequestMessagePtr&, Http::AsyncClient::Callbacks& callbacks,
                     const Http::AsyncClient::RequestOptions&) -> Http::AsyncClient::Request* {
            callbacks.onSuccess(
                Http::ResponseMessagePtr{new Http::ResponseMessageImpl(Http::ResponseHeaderMapPtr{
                    new Http::TestResponseHeaderMapImpl{{":status", "503"}}})});
            return nullptr;
          }));

  EXPECT_CALL(init_manager_, add(_)).WillOnce(Invoke([this](const Init::Target& target) {
    init_target_handle_ = target.createHandle("test");
  }));

  std::string async_data = "non-empty";
  auto provider = std::make_unique<Config::DataSource::RemoteAsyncDataProvider>(
      cm_, init_manager_, config.remote(), true,
      [&async_data](const std::string& str) { async_data = str; });

  EXPECT_CALL(init_watcher_, ready());
  init_target_handle_->initialize(init_watcher_);
  EXPECT_EQ(async_data, EMPTY_STRING);
  EXPECT_NE(nullptr, provider.get());
}

TEST_F(AsyncDataSourceTest, LoadRemoteDataSourceDoNotAllowEmptyExpectNetworkFailure) {
  AsyncDataSourcePb config;

  std::string yaml = R"EOF(
    remote:
      http_uri:
        uri: https://example.com/data
        cluster: cluster_1
      sha256:
        xxxxxx
  )EOF";
  TestUtility::loadFromYaml(yaml, config);
  EXPECT_TRUE(config.has_remote());

  EXPECT_CALL(cm_, httpAsyncClientForCluster("cluster_1")).WillOnce(ReturnRef(cm_.async_client_));
  EXPECT_CALL(cm_.async_client_, send_(_, _, _))
      .WillOnce(
          Invoke([&](Http::RequestMessagePtr&, Http::AsyncClient::Callbacks& callbacks,
                     const Http::AsyncClient::RequestOptions&) -> Http::AsyncClient::Request* {
            callbacks.onSuccess(
                Http::ResponseMessagePtr{new Http::ResponseMessageImpl(Http::ResponseHeaderMapPtr{
                    new Http::TestResponseHeaderMapImpl{{":status", "503"}}})});
            return nullptr;
          }));

  EXPECT_CALL(init_manager_, add(_)).WillOnce(Invoke([this](const Init::Target& target) {
    init_target_handle_ = target.createHandle("test");
  }));

  auto provider = std::make_unique<Config::DataSource::RemoteAsyncDataProvider>(
      cm_, init_manager_, config.remote(), false, [](const std::string&) {});

  EXPECT_CALL(init_watcher_, ready());
  init_target_handle_->initialize(init_watcher_);
  EXPECT_NE(nullptr, provider.get());
}

TEST_F(AsyncDataSourceTest, LoadRemoteDataSourceExpectInvalidData) {
  AsyncDataSourcePb config;

  std::string yaml = R"EOF(
    remote:
      http_uri:
        uri: https://example.com/data
        cluster: cluster_1
      sha256:
        xxxxxx
  )EOF";
  TestUtility::loadFromYaml(yaml, config);
  EXPECT_TRUE(config.has_remote());

  const std::string body = "hello world";

  EXPECT_CALL(cm_, httpAsyncClientForCluster("cluster_1")).WillOnce(ReturnRef(cm_.async_client_));
  EXPECT_CALL(cm_.async_client_, send_(_, _, _))
      .WillOnce(
          Invoke([&](Http::RequestMessagePtr&, Http::AsyncClient::Callbacks& callbacks,
                     const Http::AsyncClient::RequestOptions&) -> Http::AsyncClient::Request* {
            Http::ResponseMessagePtr response(
                new Http::ResponseMessageImpl(Http::ResponseHeaderMapPtr{
                    new Http::TestResponseHeaderMapImpl{{":status", "200"}}}));
            response->body() = std::make_unique<Buffer::OwnedImpl>(body);

            callbacks.onSuccess(std::move(response));
            return nullptr;
          }));

  EXPECT_CALL(init_manager_, add(_)).WillOnce(Invoke([this](const Init::Target& target) {
    init_target_handle_ = target.createHandle("test");
  }));

  auto provider = std::make_unique<Config::DataSource::RemoteAsyncDataProvider>(
      cm_, init_manager_, config.remote(), false, [](const std::string&) {});

  EXPECT_CALL(init_watcher_, ready());
  init_target_handle_->initialize(init_watcher_);
  EXPECT_NE(nullptr, provider.get());
}

TEST_F(AsyncDataSourceTest, DatasourceReleasedBeforeFetchingData) {
  const std::string body = "hello world";
  std::string async_data = "non-empty";
  std::unique_ptr<Config::DataSource::RemoteAsyncDataProvider> provider;

  {
    AsyncDataSourcePb config;

    std::string yaml = R"EOF(
    remote:
      http_uri:
        uri: https://example.com/data
        cluster: cluster_1
      sha256:
        b94d27b9934d3e08a52e52d7da7dabfac484efe37a5380ee9088f7ace2efcde9
  )EOF";
    TestUtility::loadFromYaml(yaml, config);
    EXPECT_TRUE(config.has_remote());

    EXPECT_CALL(cm_, httpAsyncClientForCluster("cluster_1")).WillOnce(ReturnRef(cm_.async_client_));
    EXPECT_CALL(cm_.async_client_, send_(_, _, _))
        .WillOnce(
            Invoke([&](Http::RequestMessagePtr&, Http::AsyncClient::Callbacks& callbacks,
                       const Http::AsyncClient::RequestOptions&) -> Http::AsyncClient::Request* {
              Http::ResponseMessagePtr response(
                  new Http::ResponseMessageImpl(Http::ResponseHeaderMapPtr{
                      new Http::TestResponseHeaderMapImpl{{":status", "200"}}}));
              response->body() = std::make_unique<Buffer::OwnedImpl>(body);

              callbacks.onSuccess(std::move(response));
              return nullptr;
            }));

    EXPECT_CALL(init_manager_, add(_)).WillOnce(Invoke([this](const Init::Target& target) {
      init_target_handle_ = target.createHandle("test");
    }));

    provider = std::make_unique<Config::DataSource::RemoteAsyncDataProvider>(
        cm_, init_manager_, config.remote(), true, [&](const std::string& data) {
          EXPECT_EQ(init_manager_.state(), Init::Manager::State::Initializing);
          EXPECT_EQ(data, body);
          async_data = data;
        });
  }

  EXPECT_CALL(init_manager_, state()).WillOnce(Return(Init::Manager::State::Initializing));
  EXPECT_CALL(init_watcher_, ready());
  init_target_handle_->initialize(init_watcher_);
  EXPECT_EQ(async_data, body);
  EXPECT_NE(nullptr, provider.get());
}

} // namespace
} // namespace Config
} // namespace Envoy
