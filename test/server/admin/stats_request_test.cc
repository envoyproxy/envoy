#include <memory>

#include "source/common/buffer/buffer_impl.h"
#include "source/common/stats/thread_local_store.h"
#include "source/server/admin/stats_request.h"

#include "test/mocks/event/mocks.h"
#include "test/mocks/stats/mocks.h"
#include "test/mocks/thread_local/mocks.h"
#include "test/test_common/utility.h"

#include "gtest/gtest.h"

using testing::NiceMock;
using testing::StartsWith;

namespace Envoy {
namespace Server {

class StatsRequestTest : public testing::Test {
protected:
  StatsRequestTest() : pool_(symbol_table_), alloc_(symbol_table_), store_(alloc_) {
    store_.addSink(sink_);
    store_.initializeThreading(main_thread_dispatcher_, tls_);
  }

  ~StatsRequestTest() override {
    tls_.shutdownGlobalThreading();
    store_.shutdownThreading();
    tls_.shutdownThread();
  }

  std::unique_ptr<StatsRequest> makeRequest(bool used_only, StatsFormat format, StatsType type) {
    StatsParams params;
    params.used_only_ = used_only;
    params.type_ = type;
    params.format_ = format;
    return std::make_unique<StatsRequest>(store_, params);
  }

  // Executes a request, counting the chunks that were generated.
  uint32_t iterateChunks(StatsRequest& request, bool drain = true,
                         Http::Code expect_code = Http::Code::OK) {
    Http::TestResponseHeaderMapImpl response_headers;
    Http::Code code = request.start(response_headers);
    EXPECT_EQ(expect_code, code);
    if (code != Http::Code::OK) {
      return 0;
    }
    Buffer::OwnedImpl data;
    uint32_t num_chunks = 0;
    bool more = true;
    do {
      more = request.nextChunk(data);
      uint64_t size = data.length();
      if (size > 0) {
        ++num_chunks;
        if (drain) {
          data.drain(size);
        }
      }
    } while (more);
    return num_chunks;
  }

  // Executes a request, returning the rendered buffer as a string.
  std::string response(StatsRequest& request) {
    Http::TestResponseHeaderMapImpl response_headers;
    Http::Code code = request.start(response_headers);
    EXPECT_EQ(Http::Code::OK, code);
    Buffer::OwnedImpl data;
    while (request.nextChunk(data)) {
    }
    return data.toString();
  }

  Stats::StatName makeStatName(absl::string_view name) { return pool_.add(name); }

  Stats::SymbolTableImpl symbol_table_;
  Stats::StatNamePool pool_;
  Stats::AllocatorImpl alloc_;
  NiceMock<Stats::MockSink> sink_;
  NiceMock<Event::MockDispatcher> main_thread_dispatcher_;
  NiceMock<ThreadLocal::MockInstance> tls_;
  Stats::ThreadLocalStoreImpl store_;
  Buffer::OwnedImpl response_;
};

TEST_F(StatsRequestTest, Empty) {
  EXPECT_EQ(0, iterateChunks(*makeRequest(false, StatsFormat::Text, StatsType::All)));
}

TEST_F(StatsRequestTest, OneCounter) {
  store_.counterFromStatName(makeStatName("foo"));
  EXPECT_EQ(1, iterateChunks(*makeRequest(false, StatsFormat::Text, StatsType::All)));
  EXPECT_EQ(1, iterateChunks(*makeRequest(false, StatsFormat::Text, StatsType::Counters)));
  EXPECT_EQ(0, iterateChunks(*makeRequest(false, StatsFormat::Text, StatsType::Gauges)));
}

TEST_F(StatsRequestTest, OneGauge) {
  store_.gaugeFromStatName(makeStatName("foo"), Stats::Gauge::ImportMode::Accumulate);
  EXPECT_EQ(1, iterateChunks(*makeRequest(false, StatsFormat::Text, StatsType::All)));
  EXPECT_EQ(1, iterateChunks(*makeRequest(false, StatsFormat::Text, StatsType::Gauges)));
  EXPECT_EQ(0, iterateChunks(*makeRequest(false, StatsFormat::Text, StatsType::Counters)));
}

TEST_F(StatsRequestTest, OneHistogram) {
  store_.histogramFromStatName(makeStatName("foo"), Stats::Histogram::Unit::Milliseconds);
  EXPECT_EQ(1, iterateChunks(*makeRequest(false, StatsFormat::Text, StatsType::All)));
  EXPECT_EQ(1, iterateChunks(*makeRequest(false, StatsFormat::Text, StatsType::Histograms)));
  EXPECT_EQ(0, iterateChunks(*makeRequest(false, StatsFormat::Text, StatsType::Counters)));
}

TEST_F(StatsRequestTest, OneTextReadout) {
  store_.textReadoutFromStatName(makeStatName("foo"));
  EXPECT_EQ(1, iterateChunks(*makeRequest(false, StatsFormat::Text, StatsType::All)));
  EXPECT_EQ(1, iterateChunks(*makeRequest(false, StatsFormat::Text, StatsType::TextReadouts)));
  EXPECT_EQ(0, iterateChunks(*makeRequest(false, StatsFormat::Text, StatsType::Counters)));
}

TEST_F(StatsRequestTest, OneScope) {
  Stats::ScopeSharedPtr scope = store_.createScope("foo");
  EXPECT_EQ(0, iterateChunks(*makeRequest(false, StatsFormat::Text, StatsType::All)));
}

TEST_F(StatsRequestTest, ManyStatsSmallChunkSize) {
  for (uint32_t i = 0; i < 100; ++i) {
    store_.counterFromStatName(makeStatName(absl::StrCat("foo", i)));
  }
  std::unique_ptr<StatsRequest> request = makeRequest(false, StatsFormat::Text, StatsType::All);
  request->setChunkSize(100);
  EXPECT_EQ(9, iterateChunks(*request));
}

TEST_F(StatsRequestTest, ManyStatsSmallChunkSizeNoDrain) {
  for (uint32_t i = 0; i < 100; ++i) {
    store_.counterFromStatName(makeStatName(absl::StrCat("foo", i)));
  }
  std::unique_ptr<StatsRequest> request = makeRequest(false, StatsFormat::Text, StatsType::All);
  request->setChunkSize(100);
  EXPECT_EQ(9, iterateChunks(*request, false));
}

TEST_F(StatsRequestTest, OneStatUsedOnly) {
  store_.counterFromStatName(makeStatName("foo"));
  EXPECT_EQ(0, iterateChunks(*makeRequest(true, StatsFormat::Text, StatsType::All)));
}

TEST_F(StatsRequestTest, OneStatJson) {
  store_.counterFromStatName(makeStatName("foo"));
  EXPECT_THAT(response(*makeRequest(false, StatsFormat::Json, StatsType::All)), StartsWith("{"));
}

TEST_F(StatsRequestTest, OneStatPrometheus) {
  // Currently the rendering infrastructure does not support Prometheus -- that
  // gets rendered using a different code-path. This will be fixed at some
  // point, to make Prometheus consume less resource, and when that occurs this
  // test can exercise that.
  store_.counterFromStatName(makeStatName("foo"));
  EXPECT_ENVOY_BUG(iterateChunks(*makeRequest(false, StatsFormat::Prometheus, StatsType::All), true,
                                 Http::Code::BadRequest),
                   "reached Prometheus case in switch unexpectedly");
}

} // namespace Server
} // namespace Envoy
