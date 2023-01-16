#include "test/server/admin/stats_request_test_base.h"

#include <string>

namespace Envoy {
namespace Server {

template <class T>
StatsRequestTestBase<T>::StatsRequestTestBase()
    : pool_(symbol_table_), alloc_(symbol_table_), store_(alloc_) {
  store_.addSink(sink_);
  store_.initializeThreading(main_thread_dispatcher_, tls_);
}

template <class T> StatsRequestTestBase<T>::~StatsRequestTestBase() {
  tls_.shutdownGlobalThreading();
  store_.shutdownThreading();
  tls_.shutdownThread();
}

// Executes a request, counting the chunks that were generated.
template <class T>
uint32_t StatsRequestTestBase<T>::iterateChunks(T& request, bool drain, Http::Code expect_code) {
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
    uint64_t prev_size = data.length();
    more = request.nextChunk(data);
    uint64_t size = data.length();
    if ((drain && size > 0) || (!drain && size != prev_size)) {
      ++num_chunks;
      if (drain) {
        data.drain(size);
      }
    }
  } while (more);
  return num_chunks;
}

// Executes a request, returning the rendered buffer as a string.
template <class T> std::string StatsRequestTestBase<T>::response(T& request) {
  Http::TestResponseHeaderMapImpl response_headers;
  Http::Code code = request.start(response_headers);
  EXPECT_EQ(Http::Code::OK, code);
  Buffer::OwnedImpl data;
  while (request.nextChunk(data)) {
  }
  return data.toString();
}

TestScope::TestScope(const std::string& prefix, Stats::MockStore& store)
    : Stats::IsolatedScopeImpl(prefix, store) {}

template class StatsRequestTestBase<GroupedStatsRequest>;
template class StatsRequestTestBase<UngroupedStatsRequest>;

} // namespace Server
} // namespace Envoy
