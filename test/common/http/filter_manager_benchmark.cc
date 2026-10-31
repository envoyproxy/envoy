#include <memory>
#include <optional>

#include "envoy/common/optref.h"
#include "envoy/config/overload/v3/overload.pb.h"
#include "envoy/http/filter.h"
#include "envoy/http/header_map.h"
#include "envoy/matcher/matcher.h"
#include "envoy/stream_info/filter_state.h"

#include "source/common/buffer/buffer_impl.h"
#include "source/common/buffer/watermark_buffer.h"
#include "source/common/http/filter_manager.h"
#include "source/common/http/header_map_impl.h"
#include "source/common/stream_info/filter_state_impl.h"
#include "source/common/stream_info/stream_info_impl.h"

#include "test/mocks/event/mocks.h"
#include "test/mocks/http/mocks.h"
#include "test/mocks/local_reply/mocks.h"
#include "test/mocks/network/mocks.h"
#include "test/mocks/server/overload_manager.h"
#include "test/test_common/test_runtime.h"

#include "benchmark/benchmark.h"

using testing::NiceMock;

namespace Envoy {
namespace Http {
namespace {

struct FilterManagerBenchmarkContext {
  NiceMock<MockFilterManagerCallbacks> filter_manager_callbacks_;
  NiceMock<Event::MockDispatcher> dispatcher_;
  NiceMock<Network::MockConnection> connection_;
  Envoy::Http::MockFilterChainFactory filter_factory_;
  NiceMock<LocalReply::MockLocalReply> local_reply_;
  Protocol protocol_{Protocol::Http2};
  NiceMock<MockTimeSystem> time_source_;
  StreamInfo::FilterStateSharedPtr filter_state_ =
      std::make_shared<StreamInfo::FilterStateImpl>(StreamInfo::FilterState::LifeSpan::Connection);
  NiceMock<Server::MockOverloadManager> overload_manager_;
};

static void BM_FilterManagerCreateDestroy(benchmark::State& state) {
  FilterManagerBenchmarkContext context;

  for (auto _ : state) {
    auto filter_manager = std::make_unique<DownstreamFilterManager>(
        context.filter_manager_callbacks_, context.dispatcher_, context.connection_, 0, nullptr,
        true, 10000, context.filter_factory_, context.local_reply_, context.protocol_,
        context.time_source_, context.filter_state_, context.overload_manager_);
    filter_manager->destroyFilters();
  }
}
BENCHMARK(BM_FilterManagerCreateDestroy);

// Which filters stop and buffer the body until the end of stream, in both directions.
enum class BufferMode { None, FirstFilter, TwoFilters };

class BenchFilter : public StreamFilter {
public:
  BenchFilter(bool buffer, bool terminal) : buffer_(buffer), terminal_(terminal) {}

  // StreamDecoderFilter
  FilterHeadersStatus decodeHeaders(RequestHeaderMap&, bool end_stream) override {
    return buffer_ && !end_stream ? FilterHeadersStatus::StopIteration
                                  : FilterHeadersStatus::Continue;
  }
  FilterDataStatus decodeData(Buffer::Instance& data, bool end_stream) override {
    if (terminal_) {
      // Consume the body as the router does.
      request_bytes_ += data.length();
      data.drain(data.length());
      return FilterDataStatus::Continue;
    }
    return buffer_ && !end_stream ? FilterDataStatus::StopIterationAndBuffer
                                  : FilterDataStatus::Continue;
  }
  FilterTrailersStatus decodeTrailers(RequestTrailerMap&) override {
    return FilterTrailersStatus::Continue;
  }
  void setDecoderFilterCallbacks(StreamDecoderFilterCallbacks& callbacks) override {
    decoder_callbacks_ = &callbacks;
  }

  // StreamEncoderFilter
  Filter1xxHeadersStatus encode1xxHeaders(ResponseHeaderMap&) override {
    return Filter1xxHeadersStatus::Continue;
  }
  FilterHeadersStatus encodeHeaders(ResponseHeaderMap&, bool end_stream) override {
    return buffer_ && !end_stream ? FilterHeadersStatus::StopIteration
                                  : FilterHeadersStatus::Continue;
  }
  FilterDataStatus encodeData(Buffer::Instance&, bool end_stream) override {
    return buffer_ && !end_stream ? FilterDataStatus::StopIterationAndBuffer
                                  : FilterDataStatus::Continue;
  }
  FilterTrailersStatus encodeTrailers(ResponseTrailerMap&) override {
    return FilterTrailersStatus::Continue;
  }
  FilterMetadataStatus encodeMetadata(MetadataMap&) override {
    return FilterMetadataStatus::Continue;
  }
  void setEncoderFilterCallbacks(StreamEncoderFilterCallbacks&) override {}

  // StreamFilterBase
  void onDestroy() override {}

  const bool buffer_;
  const bool terminal_;
  StreamDecoderFilterCallbacks* decoder_callbacks_{};
  uint64_t request_bytes_{};
};

// Creates the bench filters; the last one is terminal and never buffers.
class BenchFilterChainFactory : public MockFilterChainFactory {
public:
  bool createFilterChain(FilterChainFactoryCallbacks& callbacks) const override {
    for (size_t i = 0; i < num_filters_; i++) {
      const bool terminal = i == num_filters_ - 1;
      const bool buffer = !terminal && ((mode_ != BufferMode::None && i == 0) ||
                                        (mode_ == BufferMode::TwoFilters && i == num_filters_ / 2));
      auto filter = std::make_shared<BenchFilter>(buffer, terminal);
      if (terminal) {
        terminal_ = filter.get();
      }
      callbacks.setFilterConfigName("bench");
      callbacks.addStreamFilter(std::move(filter));
    }
    return true;
  }

  size_t num_filters_{};
  BufferMode mode_{BufferMode::None};
  mutable BenchFilter* terminal_{};
};

// Keeps gmock off the per-frame path by implementing the callbacks the filter manager calls.
class BenchFilterManagerCallbacks : public MockFilterManagerCallbacks {
public:
  void encodeHeaders(ResponseHeaderMap&, bool) override {}
  void encodeData(Buffer::Instance& data, bool) override {
    response_bytes_ += data.length();
    data.drain(data.length());
  }
  void encodeTrailers(ResponseTrailerMap&) override {}
  void chargeStats(const ResponseHeaderMap&) override {}
  void setResponseHeaders(ResponseHeaderMapPtr&& response_headers) override {
    response_headers_ = std::move(response_headers);
  }
  RequestHeaderMapOptRef requestHeaders() override { return makeOptRefFromPtr(request_headers_); }
  RequestTrailerMapOptRef requestTrailers() override { return {}; }
  ResponseHeaderMapOptRef informationalHeaders() override { return {}; }
  ResponseHeaderMapOptRef responseHeaders() override {
    return makeOptRefFromPtr(response_headers_.get());
  }
  ResponseTrailerMapOptRef responseTrailers() override { return {}; }
  void endStream() override {}
  void resetIdleTimer() override {}
  void disarmRequestTimeout() override {}
  bool isHalfCloseEnabled() override { return false; }

  RequestHeaderMap* request_headers_{};
  uint64_t response_bytes_{};
};

// Creates the stream buffers with the production factory instead of the gmock one.
class BenchDispatcher : public Event::MockDispatcher {
public:
  Buffer::WatermarkFactory& getWatermarkFactory() override { return watermark_factory_; }

  Buffer::WatermarkBufferFactory watermark_factory_{
      envoy::config::overload::v3::BufferFactoryConfig()};
};

struct FilterManagerStreamBenchmarkContext {
  NiceMock<BenchFilterManagerCallbacks> filter_manager_callbacks_;
  NiceMock<BenchDispatcher> dispatcher_;
  NiceMock<Network::MockConnection> connection_;
  NiceMock<BenchFilterChainFactory> filter_factory_;
  NiceMock<LocalReply::MockLocalReply> local_reply_;
  Protocol protocol_{Protocol::Http2};
  NiceMock<MockTimeSystem> time_source_;
  StreamInfo::FilterStateSharedPtr filter_state_ =
      std::make_shared<StreamInfo::FilterStateImpl>(StreamInfo::FilterState::LifeSpan::Connection);
  NiceMock<Server::MockOverloadManager> overload_manager_;
};

// Runs one full stream per iteration: create the chain, decode the request, encode the response
// from the terminal filter, and destroy the filters. state.range(0) is the number of filters.
static void runStreams(benchmark::State& state, BufferMode mode, bool with_body) {
  constexpr int kChunks = 4;
  const std::string chunk(1024, 'a');
  FilterManagerStreamBenchmarkContext context;
  context.filter_factory_.num_filters_ = state.range(0);
  context.filter_factory_.mode_ = mode;
  auto request_headers = RequestHeaderMapImpl::create();
  request_headers->setMethod(with_body ? "POST" : "GET");
  request_headers->setPath("/");
  request_headers->setHost("host");
  context.filter_manager_callbacks_.request_headers_ = request_headers.get();
  uint64_t request_bytes = 0;

  for (auto _ : state) {
    auto filter_manager = std::make_unique<DownstreamFilterManager>(
        context.filter_manager_callbacks_, context.dispatcher_, context.connection_, 0, nullptr,
        true, 10000, context.filter_factory_, context.local_reply_, context.protocol_,
        context.time_source_, context.filter_state_, context.overload_manager_);
    filter_manager->createDownstreamFilterChain();
    filter_manager->requestHeadersInitialized();

    filter_manager->decodeHeaders(*request_headers, !with_body);
    for (int i = 0; with_body && i < kChunks; i++) {
      Buffer::OwnedImpl data(chunk);
      filter_manager->decodeData(data, i == kChunks - 1);
    }

    BenchFilter& terminal = *context.filter_factory_.terminal_;
    auto response_headers = ResponseHeaderMapImpl::create();
    response_headers->setStatus(200);
    terminal.decoder_callbacks_->encodeHeaders(std::move(response_headers), !with_body, "details");
    for (int i = 0; with_body && i < kChunks; i++) {
      Buffer::OwnedImpl data(chunk);
      terminal.decoder_callbacks_->encodeData(data, i == kChunks - 1);
    }
    request_bytes += terminal.request_bytes_;

    filter_manager->destroyFilters();
  }

  // Guard against a scenario that silently stops the body somewhere in the chain.
  const uint64_t expected = with_body ? state.iterations() * kChunks * chunk.size() : 0;
  if (request_bytes != expected || context.filter_manager_callbacks_.response_bytes_ != expected) {
    state.SkipWithError("body did not reach the end of the filter chain");
  }
}

static void BM_FilterManagerHeaderOnly(benchmark::State& state) {
  runStreams(state, BufferMode::None, false);
}
BENCHMARK(BM_FilterManagerHeaderOnly)->Arg(3)->Arg(10);

static void BM_FilterManagerStreamingBody(benchmark::State& state) {
  runStreams(state, BufferMode::None, true);
}
BENCHMARK(BM_FilterManagerStreamingBody)->Arg(3)->Arg(10);

static void BM_FilterManagerBufferedBody(benchmark::State& state) {
  runStreams(state, BufferMode::FirstFilter, true);
}
BENCHMARK(BM_FilterManagerBufferedBody)->Arg(3)->Arg(10);

static void BM_FilterManagerBufferedBodyTwoFilters(benchmark::State& state) {
  runStreams(state, BufferMode::TwoFilters, true);
}
BENCHMARK(BM_FilterManagerBufferedBodyTwoFilters)->Arg(3)->Arg(10);

} // namespace
} // namespace Http
} // namespace Envoy
