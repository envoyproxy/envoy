#include <chrono>
#include <string>

#include "source/common/http/headers.h"
#include "source/common/http/message_impl.h"
#include "source/common/router/shadow_writer_impl.h"
#include "source/common/stats/isolated_store_impl.h"

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
          EXPECT_EQ(&callbacks, &writer_);
          holdContext(request_, inner_options);
          return &request_;
        });
    writer_.shadow("foo", std::move(message), options, stats);
  }

  void expectCluster() {
    cm_.initializeThreadLocalClusters({"foo"});
    EXPECT_CALL(cm_, getThreadLocalCluster(Eq("foo")));
    EXPECT_CALL(cm_.thread_local_cluster_, httpAsyncClient());
  }

  // Holds the context of the options the way the async client request does, until
  // destroyRequest().
  void holdContext(Http::MockAsyncClientRequest& request,
                   const Http::AsyncClient::RequestOptions& options) {
    context_ = options.context_;
    EXPECT_CALL(request, context()).WillRepeatedly(Return(makeOptRefFromPtr(context_.get())));
  }
  void destroyRequest() { context_.reset(); }

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

  Stats::IsolatedStoreImpl store_;
  Upstream::MockClusterManager cm_;
  ShadowWriterImpl writer_{cm_};
  NiceMock<Http::MockAsyncClientRequest> request_{&cm_.thread_local_cluster_.async_client_};
  Http::AsyncClient::RequestContextSharedPtr context_;
};

TEST_F(ShadowWriterImplTest, Success) {
  InSequence s;

  expectShadowWriter("cluster1", "cluster1-shadow", false);
  EXPECT_EQ(nullptr, context_);
  writer_.onSuccess(request_, response(200));
}

TEST_F(ShadowWriterImplTest, Failure) {
  InSequence s;

  expectShadowWriter("cluster1:8000", "cluster1-shadow:8000", false);
  writer_.onFailure(request_, Http::AsyncClient::FailureReason::Reset);
}

// Ensure that "-shadow" is not appended to the shadowed host if option is set.
TEST_F(ShadowWriterImplTest, SuccessNoShadowHeaderAppend) {
  InSequence s;

  expectShadowWriter("cluster2", "cluster2", true);
  writer_.onSuccess(request_, response(200));
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

  EXPECT_EQ(nullptr, options.context_);
  EXPECT_EQ(1, counter("rq_no_cluster"));
  EXPECT_EQ(0, counter("rq_started"));
}

TEST_F(ShadowWriterImplTest, StatsSuccess) {
  expectShadowWriter("cluster1", "cluster1-shadow", false, makeStats());
  ASSERT_NE(nullptr, context_);
  writer_.onSuccess(request_, response(200));
  destroyRequest();

  EXPECT_EQ(1, counter("rq_started"));
  EXPECT_EQ(1, counter("rq_success"));
  EXPECT_EQ(0, counter("rq_failure"));
  EXPECT_EQ(0, counter("rq_cancelled"));
}

TEST_F(ShadowWriterImplTest, StatsNon2xxResponseIsFailure) {
  expectShadowWriter("cluster1", "cluster1-shadow", false, makeStats());
  writer_.onSuccess(request_, response(503));
  destroyRequest();

  EXPECT_EQ(0, counter("rq_success"));
  EXPECT_EQ(1, counter("rq_failure"));
  EXPECT_EQ(0, counter("rq_cancelled"));
}

TEST_F(ShadowWriterImplTest, StatsResponseWithoutStatusIsFailure) {
  expectShadowWriter("cluster1", "cluster1-shadow", false, makeStats());
  writer_.onSuccess(request_, std::make_unique<Http::ResponseMessageImpl>());
  destroyRequest();

  EXPECT_EQ(0, counter("rq_success"));
  EXPECT_EQ(1, counter("rq_failure"));
}

TEST_F(ShadowWriterImplTest, StatsReset) {
  expectShadowWriter("cluster1", "cluster1-shadow", false, makeStats());
  writer_.onFailure(request_, Http::AsyncClient::FailureReason::Reset);
  destroyRequest();

  EXPECT_EQ(1, counter("rq_failure"));
  EXPECT_EQ(0, counter("rq_cancelled"));
}

// A cancelled request is destroyed without calling onSuccess() or onFailure().
TEST_F(ShadowWriterImplTest, StatsCancelled) {
  expectShadowWriter("cluster1", "cluster1-shadow", false, makeStats());
  EXPECT_EQ(0, counter("rq_cancelled"));
  destroyRequest();

  EXPECT_EQ(1, counter("rq_started"));
  EXPECT_EQ(0, counter("rq_success"));
  EXPECT_EQ(0, counter("rq_failure"));
  EXPECT_EQ(1, counter("rq_cancelled"));
}

// The request fails while it is being started: the callbacks run inline and the async client
// returns nullptr. The failure is counted once.
TEST_F(ShadowWriterImplTest, StatsSendImmediateFailure) {
  expectCluster();
  EXPECT_CALL(cm_.thread_local_cluster_.async_client_, send_(_, _, _))
      .WillOnce(
          [&](Http::RequestMessagePtr&, Http::AsyncClient::Callbacks& callbacks,
              const Http::AsyncClient::RequestOptions& options) -> Http::AsyncClient::Request* {
            holdContext(request_, options);
            callbacks.onFailure(request_, Http::AsyncClient::FailureReason::Reset);
            destroyRequest();
            return nullptr;
          });
  Http::RequestMessagePtr message(new Http::RequestMessageImpl());
  message->headers().setHost("cluster1");
  writer_.shadow("foo", std::move(message), Http::AsyncClient::RequestOptions(), makeStats());

  EXPECT_EQ(1, counter("rq_started"));
  EXPECT_EQ(1, counter("rq_failure"));
  EXPECT_EQ(0, counter("rq_cancelled"));
}

// The request cannot be created, so the async client never calls the callbacks.
TEST_F(ShadowWriterImplTest, StatsSendNotCreated) {
  expectCluster();
  EXPECT_CALL(cm_.thread_local_cluster_.async_client_, send_(_, _, _)).WillOnce(Return(nullptr));
  Http::RequestMessagePtr message(new Http::RequestMessageImpl());
  message->headers().setHost("cluster1");
  writer_.shadow("foo", std::move(message), Http::AsyncClient::RequestOptions(), makeStats());

  EXPECT_EQ(1, counter("rq_started"));
  EXPECT_EQ(1, counter("rq_failure"));
  EXPECT_EQ(0, counter("rq_cancelled"));
}

TEST_F(ShadowWriterImplTest, StreamingNoStats) {
  expectCluster();
  NiceMock<Http::MockAsyncClientOngoingRequest> stream(&cm_.thread_local_cluster_.async_client_);
  EXPECT_CALL(cm_.thread_local_cluster_.async_client_, startRequest_(_, _, _))
      .WillOnce([&](Http::RequestHeaderMapPtr&, Http::AsyncClient::Callbacks& callbacks,
                    const Http::AsyncClient::RequestOptions& options) {
        EXPECT_EQ(&callbacks, &writer_);
        EXPECT_EQ(nullptr, options.context_);
        return &stream;
      });
  auto headers = Http::RequestHeaderMapImpl::create();
  headers->setHost("cluster1");
  EXPECT_EQ(&stream, writer_.streamingShadow("foo", std::move(headers),
                                             Http::AsyncClient::RequestOptions(), nullptr));
}

TEST_F(ShadowWriterImplTest, StreamingStatsSuccess) {
  expectCluster();
  NiceMock<Http::MockAsyncClientOngoingRequest> stream(&cm_.thread_local_cluster_.async_client_);
  EXPECT_CALL(cm_.thread_local_cluster_.async_client_, startRequest_(_, _, _))
      .WillOnce([&](Http::RequestHeaderMapPtr&, Http::AsyncClient::Callbacks& callbacks,
                    const Http::AsyncClient::RequestOptions& options) {
        EXPECT_EQ(&callbacks, &writer_);
        holdContext(stream, options);
        return &stream;
      });
  auto headers = Http::RequestHeaderMapImpl::create();
  headers->setHost("cluster1");
  EXPECT_EQ(&stream, writer_.streamingShadow("foo", std::move(headers),
                                             Http::AsyncClient::RequestOptions(), makeStats()));
  writer_.onSuccess(stream, response(204));
  destroyRequest();

  EXPECT_EQ(1, counter("rq_started"));
  EXPECT_EQ(1, counter("rq_success"));
  EXPECT_EQ(0, counter("rq_cancelled"));
}

TEST_F(ShadowWriterImplTest, StreamingStatsCancelled) {
  expectCluster();
  NiceMock<Http::MockAsyncClientOngoingRequest> stream(&cm_.thread_local_cluster_.async_client_);
  EXPECT_CALL(cm_.thread_local_cluster_.async_client_, startRequest_(_, _, _))
      .WillOnce([&](Http::RequestHeaderMapPtr&, Http::AsyncClient::Callbacks&,
                    const Http::AsyncClient::RequestOptions& options) {
        holdContext(stream, options);
        return &stream;
      });
  auto headers = Http::RequestHeaderMapImpl::create();
  headers->setHost("cluster1");
  EXPECT_EQ(&stream, writer_.streamingShadow("foo", std::move(headers),
                                             Http::AsyncClient::RequestOptions(), makeStats()));
  EXPECT_EQ(0, counter("rq_cancelled"));
  destroyRequest();

  EXPECT_EQ(1, counter("rq_started"));
  EXPECT_EQ(0, counter("rq_failure"));
  EXPECT_EQ(1, counter("rq_cancelled"));
}

TEST_F(ShadowWriterImplTest, StreamingStatsNotCreated) {
  expectCluster();
  EXPECT_CALL(cm_.thread_local_cluster_.async_client_, startRequest_(_, _, _))
      .WillOnce(Return(nullptr));
  auto headers = Http::RequestHeaderMapImpl::create();
  headers->setHost("cluster1");
  EXPECT_EQ(nullptr, writer_.streamingShadow("foo", std::move(headers),
                                             Http::AsyncClient::RequestOptions(), makeStats()));

  EXPECT_EQ(1, counter("rq_started"));
  EXPECT_EQ(1, counter("rq_failure"));
  EXPECT_EQ(0, counter("rq_cancelled"));
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
