#include <chrono>
#include <string>

#include "source/common/http/headers.h"
#include "source/common/http/message_impl.h"
#include "source/common/router/shadow_writer_impl.h"

#include "test/mocks/upstream/cluster_manager.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::_;
using testing::Eq;
using testing::InSequence;
using testing::Invoke;
using testing::Return;

namespace Envoy {
namespace Router {
namespace {

class ShadowWriterImplTest : public testing::Test {
public:
  void expectShadowWriter(absl::string_view host, absl::string_view shadowed_host) {
    Http::RequestMessagePtr message(new Http::RequestMessageImpl());
    message->headers().setHost(host);
    cm_.initializeThreadLocalClusters({"foo"});
    EXPECT_CALL(cm_, getThreadLocalCluster(Eq("foo")));
    EXPECT_CALL(cm_.thread_local_cluster_, httpAsyncClient())
        .WillOnce(ReturnRef(cm_.thread_local_cluster_.async_client_));
    auto options = Http::AsyncClient::RequestOptions().setTimeout(std::chrono::milliseconds(5));
    EXPECT_CALL(cm_.thread_local_cluster_.async_client_, send_(_, _, options))
        .WillOnce(Invoke(
            [&](Http::RequestMessagePtr& inner_message, Http::AsyncClient::Callbacks& callbacks,
                const Http::AsyncClient::RequestOptions&) -> Http::AsyncClient::Request* {
              EXPECT_EQ(message, inner_message);
              EXPECT_EQ(shadowed_host, message->headers().getHostValue());
              callback_ = &callbacks;
              return &request_;
            }));
    writer_.shadow("foo", std::move(message), options);
  }

  Upstream::MockClusterManager cm_;
  ShadowWriterImpl writer_{cm_};
  Http::MockAsyncClientRequest request_{&cm_.thread_local_cluster_.async_client_};
  Http::AsyncClient::Callbacks* callback_{};
};

TEST_F(ShadowWriterImplTest, Success) {
  InSequence s;

  expectShadowWriter("cluster1", "cluster1-shadow");
  Http::ResponseMessagePtr response(new Http::ResponseMessageImpl());
  callback_->onSuccess(request_, std::move(response));
}

TEST_F(ShadowWriterImplTest, Failure) {
  InSequence s;

  expectShadowWriter("cluster1:8000", "cluster1-shadow:8000");
  callback_->onFailure(request_, Http::AsyncClient::FailureReason::Reset);
}

TEST_F(ShadowWriterImplTest, NoCluster) {
  InSequence s;

  Http::RequestMessagePtr message(new Http::RequestMessageImpl());
  EXPECT_CALL(cm_, getThreadLocalCluster(Eq("foo"))).WillOnce(Return(nullptr));
  EXPECT_CALL(cm_.thread_local_cluster_, httpAsyncClient()).Times(0);
  auto options = Http::AsyncClient::RequestOptions().setTimeout(std::chrono::milliseconds(5));
  writer_.shadow("foo", std::move(message), options);
}

} // namespace
} // namespace Router
} // namespace Envoy
