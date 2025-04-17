#include "envoy/config/core/v3/base.pb.h"
#include "envoy/config/core/v3/base.pb.validate.h"

#include "source/common/common/cleanup.h"
#include "source/common/common/empty_string.h"
#include "source/common/config/datasource.h"
#include "source/common/http/message_impl.h"
#include "source/common/protobuf/protobuf.h"
#include "source/extensions/common/wasm/oci/oci_async_datasource.h"

#include "test/mocks/event/mocks.h"
#include "test/mocks/init/mocks.h"
#include "test/mocks/upstream/cluster_manager.h"
#include "test/test_common/environment.h"
#include "test/test_common/utility.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace Common {
namespace Wasm {
namespace Oci {
namespace {
using ::testing::AtLeast;
using ::testing::NiceMock;
using ::testing::Return;

class OciManifestProviderTest : public testing::Test {
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

  ManifestProviderPtr oci_manifest_provider_;

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

  std::string datasource_yaml_ = R"EOF(
    remote:
      http_uri:
        uri: oci://123.dkr.ecr.us-east-1.amazonaws.com/namespace/wasm-filter:latest
        cluster: cluster_1
        timeout: 1s
      sha256:
        image_blob_sha
  )EOF";

  const std::string response_body_ = R"EOF({
  "schemaVersion": 2,
  "config": {
    "mediaType": "application/vnd.wasm.config.v1+json",
    "digest": "sha256:44136fa355b3678a1146ad16f7e8649e94fb4fc21fe77e8310c060f61caaff8a",
    "size": 2
  },
  "layers": [
    {
      "mediaType": "application/vnd.wasm.content.layer.v1+wasm",
      "digest": "sha256:4c7915b4c1f9b0c13f962998e4199ceb00db39a4a7fa4554f40ae0bed83d9510",
      "size": 1624962
    }
  ]
})EOF";
  const std::string layer_digest_ =
      "sha256:4c7915b4c1f9b0c13f962998e4199ceb00db39a4a7fa4554f40ae0bed83d9510";

  envoy::config::core::v3::HttpUri manifestUri() {
    envoy::config::core::v3::HttpUri manifest_uri;
    manifest_uri.set_cluster("cluster_1");
    manifest_uri.set_uri(
        "https://123.dkr.ecr.us-east-1.amazonaws.com/v2/namespace/wasm-filter/manifests/latest");
    manifest_uri.mutable_timeout()->set_seconds(1);
    return manifest_uri;
  }
};

TEST_F(OciManifestProviderTest, LoadRemoteDataSourceNoCluster) {
  AsyncDataSourcePb config;
  TestUtility::loadFromYamlAndValidate(datasource_yaml_, config);
  EXPECT_TRUE(config.has_remote());

  initialize(nullptr);

  std::string async_data = "non-empty";
  oci_manifest_provider_ = std::make_unique<ManifestProvider>(
      cm_, init_manager_, config.remote(), dispatcher_, random_, manifestUri(), nullptr,
      "123.dkr.ecr.us-east-1.amazonaws.com", [&](const std::string& data) {
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

TEST_F(OciManifestProviderTest, LoadRemoteDataSourceReturnFailure) {
  AsyncDataSourcePb config;
  TestUtility::loadFromYamlAndValidate(datasource_yaml_, config);
  EXPECT_TRUE(config.has_remote());

  cm_.initializeThreadLocalClusters({"cluster_1"});
  initialize([&](Http::RequestMessagePtr&, Http::AsyncClient::Callbacks& callbacks,
                 const Http::AsyncClient::RequestOptions&) -> Http::AsyncClient::Request* {
    callbacks.onFailure(request_, Envoy::Http::AsyncClient::FailureReason::Reset);
    return nullptr;
  });

  std::string async_data = "non-empty";
  oci_manifest_provider_ = std::make_unique<ManifestProvider>(
      cm_, init_manager_, config.remote(), dispatcher_, random_, manifestUri(), nullptr,
      "123.dkr.ecr.us-east-1.amazonaws.com", [&](const std::string& data) {
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

TEST_F(OciManifestProviderTest, LoadRemoteDataSourceSuccessWith503) {
  AsyncDataSourcePb config;
  TestUtility::loadFromYamlAndValidate(datasource_yaml_, config);
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
  oci_manifest_provider_ = std::make_unique<ManifestProvider>(
      cm_, init_manager_, config.remote(), dispatcher_, random_, manifestUri(), nullptr,
      "123.dkr.ecr.us-east-1.amazonaws.com", [&](const std::string& data) {
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

TEST_F(OciManifestProviderTest, LoadRemoteDataSourceSuccessWithEmptyBody) {
  AsyncDataSourcePb config;
  TestUtility::loadFromYamlAndValidate(datasource_yaml_, config);
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
  oci_manifest_provider_ = std::make_unique<ManifestProvider>(
      cm_, init_manager_, config.remote(), dispatcher_, random_, manifestUri(), nullptr,
      "123.dkr.ecr.us-east-1.amazonaws.com", [&](const std::string& data) {
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

TEST_F(OciManifestProviderTest, LoadRemoteDataSourceSuccess) {
  AsyncDataSourcePb config;
  TestUtility::loadFromYamlAndValidate(datasource_yaml_, config);
  EXPECT_TRUE(config.has_remote());

  cm_.initializeThreadLocalClusters({"cluster_1"});

  initialize([&](Http::RequestMessagePtr&, Http::AsyncClient::Callbacks& callbacks,
                 const Http::AsyncClient::RequestOptions&) -> Http::AsyncClient::Request* {
    Http::ResponseMessagePtr response(new Http::ResponseMessageImpl(
        Http::ResponseHeaderMapPtr{new Http::TestResponseHeaderMapImpl{{":status", "200"}}}));
    response->body().add(response_body_);

    callbacks.onSuccess(request_, std::move(response));
    return nullptr;
  });

  std::string async_data = "non-empty";
  oci_manifest_provider_ = std::make_unique<ManifestProvider>(
      cm_, init_manager_, config.remote(), dispatcher_, random_, manifestUri(), nullptr,
      "123.dkr.ecr.us-east-1.amazonaws.com", [&](const std::string& data) {
        EXPECT_EQ(init_manager_.state(), Init::Manager::State::Initializing);
        EXPECT_EQ(data, layer_digest_);
        async_data = data;
      });

  EXPECT_CALL(init_manager_, state()).WillOnce(Return(Init::Manager::State::Initializing));
  EXPECT_CALL(init_watcher_, ready());
  init_target_handle_->initialize(init_watcher_);

  EXPECT_EQ(async_data, layer_digest_);
}

TEST_F(OciManifestProviderTest, DatasourceReleasedBeforeFetchingData) {
  std::string async_data = "non-empty";

  {
    AsyncDataSourcePb config;
    TestUtility::loadFromYamlAndValidate(datasource_yaml_, config);
    EXPECT_TRUE(config.has_remote());

    cm_.initializeThreadLocalClusters({"cluster_1"});
    initialize([&](Http::RequestMessagePtr&, Http::AsyncClient::Callbacks& callbacks,
                   const Http::AsyncClient::RequestOptions&) -> Http::AsyncClient::Request* {
      Http::ResponseMessagePtr response(new Http::ResponseMessageImpl(
          Http::ResponseHeaderMapPtr{new Http::TestResponseHeaderMapImpl{{":status", "200"}}}));
      response->body().add(response_body_);

      callbacks.onSuccess(request_, std::move(response));
      return nullptr;
    });

    oci_manifest_provider_ = std::make_unique<ManifestProvider>(
        cm_, init_manager_, config.remote(), dispatcher_, random_, manifestUri(), nullptr,
        "123.dkr.ecr.us-east-1.amazonaws.com", [&](const std::string& data) {
          EXPECT_EQ(init_manager_.state(), Init::Manager::State::Initializing);
          EXPECT_EQ(data, layer_digest_);
          async_data = data;
        });
  }

  EXPECT_CALL(init_manager_, state()).WillOnce(Return(Init::Manager::State::Initializing));
  EXPECT_CALL(init_watcher_, ready());
  init_target_handle_->initialize(init_watcher_);
  EXPECT_EQ(async_data, layer_digest_);
}

TEST_F(OciManifestProviderTest, LoadRemoteDataSourceWithRetry) {
  AsyncDataSourcePb config;
  std::string yaml = R"EOF(
    remote:
      http_uri:
        uri: oci://123.dkr.ecr.us-east-1.amazonaws.com/namespace/wasm-filter:latest
        cluster: cluster_1
        timeout: 1s
      sha256:
        image_blob_sha
      retry_policy:
        retry_back_off:
          base_interval: 1s
        num_retries: 3
  )EOF";
  TestUtility::loadFromYamlAndValidate(yaml, config);
  EXPECT_TRUE(config.has_remote());

  cm_.initializeThreadLocalClusters({"cluster_1"});

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
  oci_manifest_provider_ = std::make_unique<ManifestProvider>(
      cm_, init_manager_, config.remote(), dispatcher_, random_, manifestUri(), nullptr,
      "123.dkr.ecr.us-east-1.amazonaws.com", [&](const std::string& data) {
        EXPECT_EQ(init_manager_.state(), Init::Manager::State::Initializing);
        EXPECT_EQ(data, layer_digest_);
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
                    response->body().add(response_body_);

                    callbacks.onSuccess(request_, std::move(response));
                    return nullptr;
                  }));
        }

        retry_timer_cb_();
      }));
  init_target_handle_->initialize(init_watcher_);

  EXPECT_EQ(async_data, layer_digest_);
}

} // namespace
} // namespace Oci
} // namespace Wasm
} // namespace Common
} // namespace Extensions
} // namespace Envoy
