#include <chrono>
#include <string>

#include "common/http/headers.h"
#include "common/http/message_impl.h"
#include "common/router/shadow_writer_impl.h"

#include "test/mocks/upstream/mocks.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::_;
using testing::Invoke;

namespace Envoy {
namespace Router {

void expectShadowWriter(absl::string_view host, absl::string_view shadowed_host) {
  Upstream::MockClusterManager cm;
  ShadowWriterImpl writer(cm);

  // Success case
  Http::MessagePtr message(new Http::RequestMessageImpl());
  message->headers().insertHost().value(std::string(host));
  EXPECT_CALL(cm, httpAsyncClientForCluster("foo")).WillOnce(ReturnRef(cm.async_client_));
  Http::MockAsyncClientRequest request(&cm.async_client_);
  Http::AsyncClient::Callbacks* callback;
  EXPECT_CALL(cm.async_client_,
              send_(_, _, absl::optional<std::chrono::milliseconds>(std::chrono::milliseconds(5))))
      .WillOnce(Invoke(
          [&](Http::MessagePtr& inner_message, Http::AsyncClient::Callbacks& callbacks,
              const absl::optional<std::chrono::milliseconds>&) -> Http::AsyncClient::Request* {
            EXPECT_EQ(message, inner_message);
            EXPECT_EQ(shadowed_host, message->headers().Host()->value().c_str());
            callback = &callbacks;
            return &request;
          }));
  writer.shadow("foo", std::move(message), std::chrono::milliseconds(5));

  Http::MessagePtr response(new Http::RequestMessageImpl());
  callback->onSuccess(std::move(response));

  // Failure case
  message.reset(new Http::RequestMessageImpl());
  message->headers().insertHost().value(std::string(host));
  EXPECT_CALL(cm, httpAsyncClientForCluster("bar")).WillOnce(ReturnRef(cm.async_client_));
  EXPECT_CALL(cm.async_client_,
              send_(_, _, absl::optional<std::chrono::milliseconds>(std::chrono::milliseconds(10))))
      .WillOnce(Invoke(
          [&](Http::MessagePtr& inner_message, Http::AsyncClient::Callbacks& callbacks,
              const absl::optional<std::chrono::milliseconds>&) -> Http::AsyncClient::Request* {
            EXPECT_EQ(message, inner_message);
            EXPECT_EQ(shadowed_host, message->headers().Host()->value().c_str());
            callback = &callbacks;
            return &request;
          }));
  writer.shadow("bar", std::move(message), std::chrono::milliseconds(10));
  callback->onFailure(Http::AsyncClient::FailureReason::Reset);
}

TEST(ShadowWriterImplTest, All) {
  expectShadowWriter("cluster1", "cluster1-shadow");
  expectShadowWriter("cluster1:8000", "cluster1-shadow:8000");
  expectShadowWriter("cluster1:80", "cluster1-shadow:80");
}

} // namespace Router
} // namespace Envoy
