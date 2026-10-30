#include <chrono>
#include <string>

#include "source/common/http/headers.h"
#include "source/common/http/message_impl.h"
#include "source/common/router/shadow_writer_impl.h"
#include "source/common/stats/isolated_store_impl.h"

#include "test/mocks/tracing/mocks.h"
#include "test/mocks/upstream/cluster_manager.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::_;
using testing::Eq;
using testing::InSequence;
using testing::NiceMock;
using testing::Return;

namespace Envoy {
namespace Router {
namespace {

class ShadowWriterImplTest : public testing::Test {
public:
  void expectShadowWriter(absl::string_view host, absl::string_view shadowed_host,
                          bool disabled_shadow_suffix,
                          const ShadowPolicyStatsSharedPtr& stats = nullptr) {
    Http::RequestMessagePtr message(new Http::RequestMessageImpl());
    message->headers().setHost(host);
    cm_.initializeThreadLocalClusters({"foo"});
    EXPECT_CALL(cm_, getThreadLocalCluster(Eq("foo")));
    EXPECT_CALL(cm_.thread_local_cluster_, httpAsyncClient())
        .WillOnce(ReturnRef(cm_.thread_local_cluster_.async_client_));
    auto options = Http::AsyncClient::RequestOptions()
                       .setTimeout(std::chrono::milliseconds(5))
                       .setIsShadowSuffixDisabled(disabled_shadow_suffix);
    EXPECT_CALL(cm_.thread_local_cluster_.async_client_, send_(_, _, options))
        .WillOnce([&](Http::RequestMessagePtr& inner_message,
                      Http::AsyncClient::Callbacks& callbacks,
                      const Http::AsyncClient::RequestOptions& inner_options)
                      -> Http::AsyncClient::Request* {
          EXPECT_EQ(message, inner_message);
          EXPECT_EQ(shadowed_host, message->headers().getHostValue());
          EXPECT_TRUE(inner_options.is_shadow);
          callback_ = &callbacks;
          return &request_;
        });
    writer_.shadow("foo", std::move(message), options, stats);
  }

  void expectCluster() {
    cm_.initializeThreadLocalClusters({"foo"});
    EXPECT_CALL(cm_, getThreadLocalCluster(Eq("foo")));
    EXPECT_CALL(cm_.thread_local_cluster_, httpAsyncClient());
  }

  ShadowPolicyStatsSharedPtr makeStats() {
    return std::make_shared<const ShadowPolicyStats>(ShadowPolicyStats{
        ALL_SHADOW_POLICY_STATS(POOL_COUNTER_PREFIX(*store_.rootScope(), "mirror.test."))});
  }

  uint64_t counter(absl::string_view name) {
    return store_.rootScope()->counterFromString(absl::StrCat("mirror.test.", name)).value();
  }

  Http::ResponseMessagePtr response(uint64_t status) {
    Http::ResponseMessagePtr response(new Http::ResponseMessageImpl());
    response->headers().setStatus(status);
    return response;
  }

  // Ends the request the way the async client does: the span is finalized first, then the
  // completion callback runs, and the deferred delete list is cleared on the next loop iteration.
  void finalize() { callback_->onBeforeFinalizeUpstreamSpan(span_, nullptr); }
  void clearDeferredDeleteList() {
    cm_.thread_local_cluster_.async_client_.dispatcher_.to_delete_.clear();
  }

  Stats::IsolatedStoreImpl store_;
  Upstream::MockClusterManager cm_;
  ShadowWriterImpl writer_{cm_};
  Http::MockAsyncClientRequest request_{&cm_.thread_local_cluster_.async_client_};
  Http::AsyncClient::Callbacks* callback_{};
  NiceMock<Tracing::MockSpan> span_;
};

TEST_F(ShadowWriterImplTest, Success) {
  InSequence s;

  expectShadowWriter("cluster1", "cluster1-shadow", false);
  Http::ResponseMessagePtr response(new Http::ResponseMessageImpl());
  callback_->onSuccess(request_, std::move(response));
}

TEST_F(ShadowWriterImplTest, Failure) {
  InSequence s;

  expectShadowWriter("cluster1:8000", "cluster1-shadow:8000", false);
  callback_->onFailure(request_, Http::AsyncClient::FailureReason::Reset);
}

// Ensure that "-shadow" is not appended to the shadowed host if option is set.
TEST_F(ShadowWriterImplTest, SuccessNoShadowHeaderAppend) {
  InSequence s;

  expectShadowWriter("cluster2", "cluster2", true);
  Http::ResponseMessagePtr response(new Http::ResponseMessageImpl());
  callback_->onSuccess(request_, std::move(response));
}

TEST_F(ShadowWriterImplTest, NoCluster) {
  InSequence s;

  Http::RequestMessagePtr message(new Http::RequestMessageImpl());
  EXPECT_CALL(cm_, getThreadLocalCluster(Eq("foo"))).WillOnce(Return(nullptr));
  EXPECT_CALL(cm_.thread_local_cluster_, httpAsyncClient()).Times(0);
  auto options = Http::AsyncClient::RequestOptions().setTimeout(std::chrono::milliseconds(5));
  writer_.shadow("foo", std::move(message), options, nullptr);
}

TEST_F(ShadowWriterImplTest, StatsNoCluster) {
  const ShadowPolicyStatsSharedPtr stats = makeStats();
  Http::RequestMessagePtr message(new Http::RequestMessageImpl());
  EXPECT_CALL(cm_, getThreadLocalCluster(Eq("foo"))).WillOnce(Return(nullptr));
  EXPECT_CALL(cm_.thread_local_cluster_, httpAsyncClient()).Times(0);
  auto options = Http::AsyncClient::RequestOptions().setTimeout(std::chrono::milliseconds(5));
  writer_.shadow("foo", std::move(message), options, stats);

  EXPECT_EQ(1, counter("rq_no_cluster"));
  EXPECT_EQ(0, counter("rq_started"));
}

TEST_F(ShadowWriterImplTest, StatsSuccess) {
  expectShadowWriter("cluster1", "cluster1-shadow", false, makeStats());
  EXPECT_NE(callback_, &writer_);
  finalize();
  callback_->onSuccess(request_, response(200));
  clearDeferredDeleteList();

  EXPECT_EQ(1, counter("rq_started"));
  EXPECT_EQ(1, counter("rq_success"));
  EXPECT_EQ(0, counter("rq_failure"));
  EXPECT_EQ(0, counter("rq_cancelled"));
}

TEST_F(ShadowWriterImplTest, StatsNon2xxResponseIsFailure) {
  expectShadowWriter("cluster1", "cluster1-shadow", false, makeStats());
  finalize();
  callback_->onSuccess(request_, response(503));
  clearDeferredDeleteList();

  EXPECT_EQ(0, counter("rq_success"));
  EXPECT_EQ(1, counter("rq_failure"));
  EXPECT_EQ(0, counter("rq_cancelled"));
}

TEST_F(ShadowWriterImplTest, StatsResponseWithoutStatusIsFailure) {
  expectShadowWriter("cluster1", "cluster1-shadow", false, makeStats());
  finalize();
  callback_->onSuccess(request_, std::make_unique<Http::ResponseMessageImpl>());
  clearDeferredDeleteList();

  EXPECT_EQ(0, counter("rq_success"));
  EXPECT_EQ(1, counter("rq_failure"));
}

TEST_F(ShadowWriterImplTest, StatsReset) {
  expectShadowWriter("cluster1", "cluster1-shadow", false, makeStats());
  finalize();
  callback_->onFailure(request_, Http::AsyncClient::FailureReason::Reset);
  clearDeferredDeleteList();

  EXPECT_EQ(1, counter("rq_failure"));
  EXPECT_EQ(0, counter("rq_cancelled"));
}

TEST_F(ShadowWriterImplTest, StatsCancelled) {
  expectShadowWriter("cluster1", "cluster1-shadow", false, makeStats());
  // A cancelled request finalizes its span but calls neither onSuccess() nor onFailure().
  finalize();
  EXPECT_EQ(0, counter("rq_cancelled"));
  clearDeferredDeleteList();

  EXPECT_EQ(1, counter("rq_started"));
  EXPECT_EQ(0, counter("rq_success"));
  EXPECT_EQ(0, counter("rq_failure"));
  EXPECT_EQ(1, counter("rq_cancelled"));
}

TEST_F(ShadowWriterImplTest, StatsFinalizedOnce) {
  expectShadowWriter("cluster1", "cluster1-shadow", false, makeStats());
  finalize();
  finalize();
  callback_->onSuccess(request_, response(200));
  EXPECT_EQ(1, cm_.thread_local_cluster_.async_client_.dispatcher_.to_delete_.size());
  clearDeferredDeleteList();

  EXPECT_EQ(1, counter("rq_success"));
}

// The request cannot be created, so the async client never calls the callbacks.
TEST_F(ShadowWriterImplTest, StatsSendNotStarted) {
  const ShadowPolicyStatsSharedPtr stats = makeStats();
  expectCluster();
  EXPECT_CALL(cm_.thread_local_cluster_.async_client_, send_(_, _, _)).WillOnce(Return(nullptr));
  Http::RequestMessagePtr message(new Http::RequestMessageImpl());
  message->headers().setHost("cluster1");
  writer_.shadow("foo", std::move(message), Http::AsyncClient::RequestOptions(), stats);

  EXPECT_EQ(1, counter("rq_started"));
  EXPECT_EQ(1, counter("rq_failure"));
  EXPECT_TRUE(cm_.thread_local_cluster_.async_client_.dispatcher_.to_delete_.empty());
}

// The request fails while it is being started: the callbacks run inline and the async client
// returns nullptr. The failure is counted once.
TEST_F(ShadowWriterImplTest, StatsSendImmediateFailure) {
  const ShadowPolicyStatsSharedPtr stats = makeStats();
  expectCluster();
  EXPECT_CALL(cm_.thread_local_cluster_.async_client_, send_(_, _, _))
      .WillOnce([&](Http::RequestMessagePtr&, Http::AsyncClient::Callbacks& callbacks,
                    const Http::AsyncClient::RequestOptions&) -> Http::AsyncClient::Request* {
        callbacks.onBeforeFinalizeUpstreamSpan(span_, nullptr);
        callbacks.onFailure(request_, Http::AsyncClient::FailureReason::Reset);
        return nullptr;
      });
  Http::RequestMessagePtr message(new Http::RequestMessageImpl());
  message->headers().setHost("cluster1");
  writer_.shadow("foo", std::move(message), Http::AsyncClient::RequestOptions(), stats);
  clearDeferredDeleteList();

  EXPECT_EQ(1, counter("rq_failure"));
  EXPECT_EQ(0, counter("rq_cancelled"));
}

TEST_F(ShadowWriterImplTest, StreamingNoStats) {
  expectCluster();
  Http::MockAsyncClientOngoingRequest stream(&cm_.thread_local_cluster_.async_client_);
  EXPECT_CALL(cm_.thread_local_cluster_.async_client_, startRequest_(_, _, _))
      .WillOnce([&](Http::RequestHeaderMapPtr&, Http::AsyncClient::Callbacks& callbacks,
                    const Http::AsyncClient::RequestOptions&) {
        EXPECT_EQ(&callbacks, &writer_);
        return &stream;
      });
  auto headers = Http::RequestHeaderMapImpl::create();
  headers->setHost("cluster1");
  EXPECT_EQ(&stream, writer_.streamingShadow("foo", std::move(headers),
                                             Http::AsyncClient::RequestOptions(), nullptr));
}

TEST_F(ShadowWriterImplTest, StreamingStatsSuccess) {
  const ShadowPolicyStatsSharedPtr stats = makeStats();
  expectCluster();
  Http::MockAsyncClientOngoingRequest stream(&cm_.thread_local_cluster_.async_client_);
  EXPECT_CALL(cm_.thread_local_cluster_.async_client_, startRequest_(_, _, _))
      .WillOnce([&](Http::RequestHeaderMapPtr&, Http::AsyncClient::Callbacks& callbacks,
                    const Http::AsyncClient::RequestOptions&) {
        callback_ = &callbacks;
        return &stream;
      });
  auto headers = Http::RequestHeaderMapImpl::create();
  headers->setHost("cluster1");
  EXPECT_EQ(&stream, writer_.streamingShadow("foo", std::move(headers),
                                             Http::AsyncClient::RequestOptions(), stats));
  finalize();
  callback_->onSuccess(stream, response(204));
  clearDeferredDeleteList();

  EXPECT_EQ(1, counter("rq_started"));
  EXPECT_EQ(1, counter("rq_success"));
}

TEST_F(ShadowWriterImplTest, StreamingStatsNotStarted) {
  const ShadowPolicyStatsSharedPtr stats = makeStats();
  expectCluster();
  EXPECT_CALL(cm_.thread_local_cluster_.async_client_, startRequest_(_, _, _))
      .WillOnce(Return(nullptr));
  auto headers = Http::RequestHeaderMapImpl::create();
  headers->setHost("cluster1");
  EXPECT_EQ(nullptr, writer_.streamingShadow("foo", std::move(headers),
                                             Http::AsyncClient::RequestOptions(), stats));

  EXPECT_EQ(1, counter("rq_started"));
  EXPECT_EQ(1, counter("rq_failure"));
}

TEST_F(ShadowWriterImplTest, StreamingStatsNoCluster) {
  const ShadowPolicyStatsSharedPtr stats = makeStats();
  EXPECT_CALL(cm_, getThreadLocalCluster(Eq("foo"))).WillOnce(Return(nullptr));
  auto headers = Http::RequestHeaderMapImpl::create();
  EXPECT_EQ(nullptr, writer_.streamingShadow("foo", std::move(headers),
                                             Http::AsyncClient::RequestOptions(), stats));

  EXPECT_EQ(1, counter("rq_no_cluster"));
}

} // namespace
} // namespace Router
} // namespace Envoy
