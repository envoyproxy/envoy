#include "envoy/api/v2/core/base.pb.h"
#include "envoy/api/v2/core/base.pb.validate.h"

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

  using AsyncDataSourcePb = envoy::api::v2::core::AsyncDataSource;

  NiceMock<Upstream::MockClusterManager> cm_;
  Init::MockManager init_manager_;
  Init::ExpectableWatcherImpl init_watcher_;
  Init::TargetHandlePtr init_target_handle_;
  Api::ApiPtr api_;
};

TEST_F(AsyncDataSourceTest, loadLocalDataSource) {
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

TEST_F(AsyncDataSourceTest, loadRemoteDataSourceReturnFailure) {
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
          Invoke([&](Http::MessagePtr&, Http::AsyncClient::Callbacks& callbacks,
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

TEST_F(AsyncDataSourceTest, loadRemoteDataSourceSuccessWith503) {
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
          Invoke([&](Http::MessagePtr&, Http::AsyncClient::Callbacks& callbacks,
                     const Http::AsyncClient::RequestOptions&) -> Http::AsyncClient::Request* {
            callbacks.onSuccess(Http::MessagePtr{new Http::ResponseMessageImpl(
                Http::HeaderMapPtr{new Http::TestHeaderMapImpl{{":status", "503"}}})});
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

TEST_F(AsyncDataSourceTest, loadRemoteDataSourceSuccessWithEmptyBody) {
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
          Invoke([&](Http::MessagePtr&, Http::AsyncClient::Callbacks& callbacks,
                     const Http::AsyncClient::RequestOptions&) -> Http::AsyncClient::Request* {
            callbacks.onSuccess(Http::MessagePtr{new Http::ResponseMessageImpl(
                Http::HeaderMapPtr{new Http::TestHeaderMapImpl{{":status", "200"}}})});
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

TEST_F(AsyncDataSourceTest, loadRemoteDataSourceSuccessIncorrectSha256) {
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
          Invoke([&](Http::MessagePtr&, Http::AsyncClient::Callbacks& callbacks,
                     const Http::AsyncClient::RequestOptions&) -> Http::AsyncClient::Request* {
            Http::MessagePtr response(new Http::ResponseMessageImpl(
                Http::HeaderMapPtr{new Http::TestHeaderMapImpl{{":status", "200"}}}));
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

TEST_F(AsyncDataSourceTest, loadRemoteDataSourceSuccess) {
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
          Invoke([&](Http::MessagePtr&, Http::AsyncClient::Callbacks& callbacks,
                     const Http::AsyncClient::RequestOptions&) -> Http::AsyncClient::Request* {
            Http::MessagePtr response(new Http::ResponseMessageImpl(
                Http::HeaderMapPtr{new Http::TestHeaderMapImpl{{":status", "200"}}}));
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

TEST_F(AsyncDataSourceTest, loadRemoteDataSourceExpectNetworkFailure) {
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
          Invoke([&](Http::MessagePtr&, Http::AsyncClient::Callbacks& callbacks,
                     const Http::AsyncClient::RequestOptions&) -> Http::AsyncClient::Request* {
            callbacks.onSuccess(Http::MessagePtr{new Http::ResponseMessageImpl(
                Http::HeaderMapPtr{new Http::TestHeaderMapImpl{{":status", "503"}}})});
            return nullptr;
          }));

  EXPECT_CALL(init_manager_, add(_)).WillOnce(Invoke([this](const Init::Target& target) {
    init_target_handle_ = target.createHandle("test");
  }));

  auto provider = std::make_unique<Config::DataSource::RemoteAsyncDataProvider>(
      cm_, init_manager_, config.remote(), false, [](const std::string&) {});

  EXPECT_THROW_WITH_MESSAGE(init_target_handle_->initialize(init_watcher_), EnvoyException,
                            "Failed to fetch remote data. Failure reason: 0");
  EXPECT_NE(nullptr, provider.get());
  EXPECT_CALL(init_watcher_, ready());
}

TEST_F(AsyncDataSourceTest, loadRemoteDataSourceExpectInvalidData) {
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
          Invoke([&](Http::MessagePtr&, Http::AsyncClient::Callbacks& callbacks,
                     const Http::AsyncClient::RequestOptions&) -> Http::AsyncClient::Request* {
            Http::MessagePtr response(new Http::ResponseMessageImpl(
                Http::HeaderMapPtr{new Http::TestHeaderMapImpl{{":status", "200"}}}));
            response->body() = std::make_unique<Buffer::OwnedImpl>(body);

            callbacks.onSuccess(std::move(response));
            return nullptr;
          }));

  EXPECT_CALL(init_manager_, add(_)).WillOnce(Invoke([this](const Init::Target& target) {
    init_target_handle_ = target.createHandle("test");
  }));

  auto provider = std::make_unique<Config::DataSource::RemoteAsyncDataProvider>(
      cm_, init_manager_, config.remote(), false, [](const std::string&) {});

  EXPECT_THROW_WITH_MESSAGE(init_target_handle_->initialize(init_watcher_), EnvoyException,
                            "Failed to fetch remote data. Failure reason: 1");
  EXPECT_NE(nullptr, provider.get());
  EXPECT_CALL(init_watcher_, ready());
}

} // namespace
} // namespace Config
} // namespace Envoy