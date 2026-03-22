#include <memory>
#include <string>
#include <vector>

#include "envoy/http/async_client.h"
#include "envoy/http/header_map.h"

#include "source/common/buffer/buffer_impl.h"
#include "source/common/http/muxdemux.h"

#include "test/mocks/http/mocks.h"
#include "test/mocks/server/factory_context.h"
#include "test/test_common/status_utility.h"
#include "test/test_common/utility.h"

#include "absl/status/status.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "ocpdiag/core/testing/status_matchers.h"

namespace Envoy {
namespace Http {
namespace {

using Server::Configuration::MockFactoryContext;
using StatusHelpers::StatusIs;
using ::testing::NiceMock;

class HttpMuxDemuxTest : public testing::Test {
public:
  RequestHeaderMapPtr makeRequestHeaders() {
    return RequestHeaderMapPtr{
        new TestRequestHeaderMapImpl{{":method", "POST"}, {"host", "host"}, {"path", "/mcp"}}};
  }

  ResponseHeaderMapPtr makeResponseHeaders() {
    return ResponseHeaderMapPtr{new TestResponseHeaderMapImpl{{":status", "200"}}};
  }

  ResponseTrailerMapPtr makeResponseTrailers() {
    return ResponseTrailerMapPtr{new TestResponseTrailerMapImpl{{"foo", "bar"}}};
  }

  std::shared_ptr<MockAsyncClientStreamCallbacks> makeAsyncClientStreamCallbacks() {
    return std::make_shared<NiceMock<MockAsyncClientStreamCallbacks>>();
  }

  void initializeThreadLocalClusters(const std::vector<std::string>& clusters) {
    factory_context_.server_factory_context_.cluster_manager_.initializeThreadLocalClusters(
        clusters);
    EXPECT_CALL(factory_context_.server_factory_context_.cluster_manager_.thread_local_cluster_
                    .async_client_,
                start(_, _))
        .WillRepeatedly([this](Http::AsyncClient::StreamCallbacks& callbacks,
                               const Http::AsyncClient::StreamOptions&) {
          http_callbacks_.push_back(&callbacks);
          http_streams_.emplace_back(std::make_unique<NiceMock<MockAsyncClientStream>>());
          return http_streams_.back().get();
        });
  }

  NiceMock<MockFactoryContext> factory_context_;
  std::vector<AsyncClient::StreamCallbacks*> http_callbacks_;
  std::vector<std::unique_ptr<NiceMock<MockAsyncClientStream>>> http_streams_;
};

TEST_F(HttpMuxDemuxTest, MulticastFailsWithoutClusters) {
  auto multiplexer = MuxDemux::create(factory_context_);
  auto callbacks = makeAsyncClientStreamCallbacks();
  // If no provided clusters exist, multicast call fails.
  EXPECT_THAT(multiplexer->multicast(AsyncClient::StreamOptions(),
                                     {
                                         {
                                             .cluster_name = "cluster1",
                                             .callbacks = callbacks,
                                             .options = absl::nullopt,
                                         },
                                         {
                                             .cluster_name = "cluster2",
                                             .callbacks = callbacks,
                                             .options = absl::nullopt,
                                         },
                                     }),
              StatusIs(absl::StatusCode::kInternal));
}

TEST_F(HttpMuxDemuxTest, MulticastFailsWithNoStreamsStarted) {
  auto multiplexer = MuxDemux::create(factory_context_);
  auto callbacks = makeAsyncClientStreamCallbacks();
  // Add clusters. The fake HttpClient does not create streams by default.
  factory_context_.server_factory_context_.cluster_manager_.initializeThreadLocalClusters(
      {"cluster1", "cluster2"});
  EXPECT_THAT(multiplexer->multicast(AsyncClient::StreamOptions(),
                                     {
                                         {
                                             .cluster_name = "cluster1",
                                             .callbacks = callbacks,
                                             .options = absl::nullopt,
                                         },
                                         {
                                             .cluster_name = "cluster2",
                                             .callbacks = callbacks,
                                             .options = absl::nullopt,
                                         },
                                     }),
              StatusIs(absl::StatusCode::kInternal));
}

TEST_F(HttpMuxDemuxTest, IdleInvariants) {
  auto multiplexer = MuxDemux::create(factory_context_);
  // Initial state is idle.
  EXPECT_TRUE(multiplexer->isIdle());
  // If multicast call fails, multiplexer remains idle.
  EXPECT_THAT(multiplexer->multicast(AsyncClient::StreamOptions(), {}),
              StatusIs(absl::StatusCode::kInvalidArgument));
  EXPECT_TRUE(multiplexer->isIdle());

  initializeThreadLocalClusters({"cluster"});
  // If multicast call succeeds, multiplexer is not idle.
  auto callbacks = makeAsyncClientStreamCallbacks();
  ASSERT_OK_AND_ASSIGN(auto multistream, multiplexer->multicast(AsyncClient::StreamOptions(),
                                                                {
                                                                    {
                                                                        .cluster_name = "cluster",
                                                                        .callbacks = callbacks,
                                                                        .options = absl::nullopt,
                                                                    },
                                                                }));
  EXPECT_FALSE(multiplexer->isIdle());
  EXPECT_FALSE(multistream->isIdle());

  // Reset the only stream, and multiplexer must switch to idle.
  EXPECT_CALL(*callbacks, onReset());
  http_callbacks_[0]->onReset();
  EXPECT_TRUE(multiplexer->isIdle());
  EXPECT_TRUE(multistream->isIdle());
}

TEST_F(HttpMuxDemuxTest, Multicast) {
  auto multiplexer = MuxDemux::create(factory_context_);
  initializeThreadLocalClusters({"cluster1", "cluster2"});
  auto callbacks1 = makeAsyncClientStreamCallbacks();
  auto callbacks2 = makeAsyncClientStreamCallbacks();
  ASSERT_OK_AND_ASSIGN(auto multistream, multiplexer->multicast(AsyncClient::StreamOptions(),
                                                                {
                                                                    {
                                                                        .cluster_name = "cluster1",
                                                                        .callbacks = callbacks1,
                                                                        .options = absl::nullopt,
                                                                    },
                                                                    {
                                                                        .cluster_name = "cluster2",
                                                                        .callbacks = callbacks2,
                                                                        .options = absl::nullopt,
                                                                    },
                                                                }));
  EXPECT_FALSE(multiplexer->isIdle());
  EXPECT_FALSE(multistream->isIdle());

  // Send headers, data and trailers to all streams.
  EXPECT_CALL(*http_streams_[0], sendHeaders(_, false));
  EXPECT_CALL(*http_streams_[1], sendHeaders(_, false));
  auto headers = makeRequestHeaders();
  multistream->multicastHeaders(*headers, false);

  EXPECT_CALL(*http_streams_[0], sendData(_, false)).WillOnce([](Buffer::Instance& data, bool) {
    data.drain(data.length());
  });
  // Make sure draining of the buffer does not affect other sendData calls.
  EXPECT_CALL(*http_streams_[1], sendData(_, false)).WillOnce([](Buffer::Instance& data, bool) {
    EXPECT_EQ(data.toString(), "data");
  });
  auto data = Buffer::OwnedImpl("data");
  multistream->multicastData(data, false);

  auto trailers = RequestTrailerMapPtr{new TestRequestTrailerMapImpl{{"foo", "bar"}}};
  EXPECT_CALL(*http_streams_[0], sendTrailers(_));
  EXPECT_CALL(*http_streams_[1], sendTrailers(_));
  multistream->multicastTrailers(*trailers);

  // Get responses from both streams.
  EXPECT_CALL(*callbacks1, onHeaders_(_, false));
  EXPECT_CALL(*callbacks2, onHeaders_(_, false));
  http_callbacks_[0]->onHeaders(makeResponseHeaders(), false);
  http_callbacks_[1]->onHeaders(makeResponseHeaders(), false);

  EXPECT_CALL(*callbacks1, onData(_, false));
  EXPECT_CALL(*callbacks2, onData(_, false));
  http_callbacks_[0]->onData(data, false);
  http_callbacks_[1]->onData(data, false);

  EXPECT_CALL(*callbacks1, onTrailers_(_));
  EXPECT_CALL(*callbacks2, onTrailers_(_));
  http_callbacks_[0]->onTrailers(makeResponseTrailers());
  http_callbacks_[1]->onTrailers(makeResponseTrailers());

  EXPECT_CALL(*callbacks1, onComplete());
  EXPECT_CALL(*callbacks2, onComplete());
  http_callbacks_[0]->onComplete();
  http_callbacks_[1]->onComplete();

  EXPECT_TRUE(multiplexer->isIdle());
  EXPECT_TRUE(multistream->isIdle());
}

TEST_F(HttpMuxDemuxTest, DeletingMultistreamResetsActiveStareams) {
  auto multiplexer = MuxDemux::create(factory_context_);
  initializeThreadLocalClusters({"cluster1", "cluster2"});
  auto callbacks1 = makeAsyncClientStreamCallbacks();
  auto callbacks2 = makeAsyncClientStreamCallbacks();
  ASSERT_OK_AND_ASSIGN(auto multistream, multiplexer->multicast(AsyncClient::StreamOptions(),
                                                                {
                                                                    {
                                                                        .cluster_name = "cluster1",
                                                                        .callbacks = callbacks1,
                                                                        .options = absl::nullopt,
                                                                    },
                                                                    {
                                                                        .cluster_name = "cluster2",
                                                                        .callbacks = callbacks2,
                                                                        .options = absl::nullopt,
                                                                    },
                                                                }));

  EXPECT_CALL(*http_streams_[0], sendHeaders(_, true));
  EXPECT_CALL(*http_streams_[1], sendHeaders(_, true));
  auto headers = makeRequestHeaders();
  multistream->multicastHeaders(*headers, true);

  // Complete stream 2, but leave stream 1 incomplete.
  EXPECT_CALL(*callbacks2, onHeaders_(_, true));
  http_callbacks_[1]->onHeaders(makeResponseHeaders(), true);
  EXPECT_CALL(*callbacks2, onComplete());
  http_callbacks_[1]->onComplete();

  EXPECT_CALL(*http_streams_[0], reset());
  multistream.reset();

  EXPECT_TRUE(multiplexer->isIdle());
}

TEST_F(HttpMuxDemuxTest, MulticastWithPerBackendOptions) {
  auto multiplexer = MuxDemux::create(factory_context_);

  // Track the options passed to start() for each stream
  std::vector<AsyncClient::StreamOptions> captured_options;
  factory_context_.server_factory_context_.cluster_manager_.initializeThreadLocalClusters(
      {"cluster1", "cluster2"});
  EXPECT_CALL(
      factory_context_.server_factory_context_.cluster_manager_.thread_local_cluster_.async_client_,
      start(_, _))
      .WillRepeatedly([this, &captured_options](Http::AsyncClient::StreamCallbacks& callbacks,
                                                const Http::AsyncClient::StreamOptions& options) {
        captured_options.push_back(options);
        http_callbacks_.push_back(&callbacks);
        http_streams_.emplace_back(std::make_unique<NiceMock<MockAsyncClientStream>>());
        return http_streams_.back().get();
      });

  auto callbacks1 = makeAsyncClientStreamCallbacks();
  auto callbacks2 = makeAsyncClientStreamCallbacks();

  // Create per-backend options with different timeouts
  AsyncClient::StreamOptions options1;
  options1.setTimeout(std::chrono::milliseconds(1000));
  AsyncClient::StreamOptions options2;
  options2.setTimeout(std::chrono::milliseconds(5000));

  ASSERT_OK_AND_ASSIGN(auto multistream, multiplexer->multicast(AsyncClient::StreamOptions(),
                                                                {
                                                                    {
                                                                        .cluster_name = "cluster1",
                                                                        .callbacks = callbacks1,
                                                                        .options = options1,
                                                                    },
                                                                    {
                                                                        .cluster_name = "cluster2",
                                                                        .callbacks = callbacks2,
                                                                        .options = options2,
                                                                    },
                                                                }));

  // Verify that per-backend options were used
  ASSERT_EQ(captured_options.size(), 2);
  EXPECT_EQ(captured_options[0].timeout, std::chrono::milliseconds(1000));
  EXPECT_EQ(captured_options[1].timeout, std::chrono::milliseconds(5000));

  // Complete the streams to clean up
  EXPECT_CALL(*callbacks1, onReset());
  EXPECT_CALL(*callbacks2, onReset());
  http_callbacks_[0]->onReset();
  http_callbacks_[1]->onReset();
}

TEST_F(HttpMuxDemuxTest, MulticastDifferentHeaders) {
  auto multiplexer = MuxDemux::create(factory_context_);
  initializeThreadLocalClusters({"cluster1", "cluster2"});
  auto callbacks1 = makeAsyncClientStreamCallbacks();
  auto callbacks2 = makeAsyncClientStreamCallbacks();
  ASSERT_OK_AND_ASSIGN(auto multistream, multiplexer->multicast(AsyncClient::StreamOptions(),
                                                                {
                                                                    {
                                                                        .cluster_name = "cluster1",
                                                                        .callbacks = callbacks1,
                                                                        .options = absl::nullopt,
                                                                    },
                                                                    {
                                                                        .cluster_name = "cluster2",
                                                                        .callbacks = callbacks2,
                                                                        .options = absl::nullopt,
                                                                    },
                                                                }));

  // Send different headers and data.
  EXPECT_CALL(*http_streams_[0], sendHeaders(_, false));
  EXPECT_CALL(*http_streams_[1], sendHeaders(_, false));
  for (auto stream : *multistream) {
    auto headers = makeRequestHeaders();
    stream->sendHeaders(*headers, false);
  }

  EXPECT_CALL(*http_streams_[0], sendData(_, true));
  EXPECT_CALL(*http_streams_[1], sendData(_, true));
  for (auto stream : *multistream) {
    auto data = Buffer::OwnedImpl("data");
    stream->sendData(data, true);
  }

  // Get responses from both streams.
  EXPECT_CALL(*callbacks1, onHeaders_(_, false));
  EXPECT_CALL(*callbacks2, onHeaders_(_, false));
  http_callbacks_[0]->onHeaders(makeResponseHeaders(), false);
  http_callbacks_[1]->onHeaders(makeResponseHeaders(), false);

  EXPECT_CALL(*callbacks1, onData(_, true));
  EXPECT_CALL(*callbacks2, onData(_, true));
  auto data = Buffer::OwnedImpl("data");
  http_callbacks_[0]->onData(data, true);
  http_callbacks_[1]->onData(data, true);

  EXPECT_CALL(*callbacks1, onComplete());
  EXPECT_CALL(*callbacks2, onComplete());
  http_callbacks_[0]->onComplete();
  http_callbacks_[1]->onComplete();

  EXPECT_TRUE(multiplexer->isIdle());
  EXPECT_TRUE(multistream->isIdle());
}

} // namespace
} // namespace Http
} // namespace Envoy
