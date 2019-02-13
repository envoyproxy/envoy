#include <chrono>
#include <string>

#include "common/http/headers.h"
#include "common/http/message_impl.h"
#include "common/router/shadow_writer_impl.h"

#include "test/mocks/upstream/mocks.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::_;
using testing::InSequence;
using testing::Invoke;
using testing::Return;

namespace Envoy {
namespace Router {

class ShadowWriterImplTest : public testing::Test {
public:
  void expectShadowWriter(absl::string_view host, absl::string_view shadowed_host) {
    Http::MessagePtr message(new Http::RequestMessageImpl());
    message->headers().insertHost().value(std::string(host));
    EXPECT_CALL(cm_, get("foo"));
    EXPECT_CALL(cm_, httpAsyncClientForCluster("foo")).WillOnce(ReturnRef(cm_.async_client_));
    Http::MockAsyncClientRequest request(&cm_.async_client_);
    EXPECT_CALL(
        cm_.async_client_,
        send_(_, _, Http::AsyncClient::RequestOptions().setTimeout(std::chrono::milliseconds(5))))
        .WillOnce(
            Invoke([&](Http::MessagePtr& inner_message, Http::AsyncClient::Callbacks& callbacks,
                       const Http::AsyncClient::RequestOptions&) -> Http::AsyncClient::Request* {
              EXPECT_EQ(message, inner_message);
              EXPECT_EQ(shadowed_host, message->headers().Host()->value().c_str());
              callback_ = &callbacks;
              return &request;
            }));
    writer_.shadow("foo", std::move(message), std::chrono::milliseconds(5));
  }

  Upstream::MockClusterManager cm_;
  ShadowWriterImpl writer_{cm_};
  Http::AsyncClient::Callbacks* callback_{};
};

TEST_F(ShadowWriterImplTest, Success) {
  InSequence s;

  expectShadowWriter("cluster1", "cluster1-shadow");
  Http::MessagePtr response(new Http::RequestMessageImpl());
  callback_->onSuccess(std::move(response));
}

TEST_F(ShadowWriterImplTest, Failure) {
  InSequence s;

  expectShadowWriter("cluster1:8000", "cluster1-shadow:8000");
  callback_->onFailure(Http::AsyncClient::FailureReason::Reset);
}

TEST_F(ShadowWriterImplTest, NoCluster) {
  InSequence s;

  Http::MessagePtr message(new Http::RequestMessageImpl());
  EXPECT_CALL(cm_, get("foo")).WillOnce(Return(nullptr));
  EXPECT_CALL(cm_, httpAsyncClientForCluster("foo")).Times(0);
  writer_.shadow("foo", std::move(message), std::chrono::milliseconds(5));
}

} // namespace Router
} // namespace Envoy
