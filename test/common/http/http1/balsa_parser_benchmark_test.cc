#include <cstddef>
#include <cstdint>
#include <string>

#include "source/common/http/http1/balsa_parser.h"

#include "benchmark/benchmark.h"

namespace Envoy {
namespace Http {
namespace Http1 {
namespace {

constexpr char kValidHttpTokenCharacters[] =
    "!#$%&'*+-.0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ^_`abcdefghijklmnopqrstuvwxyz|~";

enum class HeaderNameShape : int {
  LongTokenName = 0,
  ShortName = 1,
  FullTokenSetName = 2,
};

const char* headerNameShapeLabel(const HeaderNameShape shape) {
  switch (shape) {
  case HeaderNameShape::LongTokenName:
    return "long";
  case HeaderNameShape::ShortName:
    return "short";
  case HeaderNameShape::FullTokenSetName:
    return "full-token-set";
  }
  return "unknown";
}

class HeaderCountingCallbacks : public ParserCallbacks {
public:
  CallbackResult onMessageBegin() override { return CallbackResult::Success; }
  CallbackResult onUrl(const char*, size_t length) override {
    bytes_seen_ += length;
    return CallbackResult::Success;
  }
  CallbackResult onStatus(const char*, size_t length) override {
    bytes_seen_ += length;
    return CallbackResult::Success;
  }
  CallbackResult onHeaderField(const char*, size_t length) override {
    bytes_seen_ += length;
    ++headers_seen_;
    return CallbackResult::Success;
  }
  CallbackResult onHeaderValue(const char*, size_t length) override {
    bytes_seen_ += length;
    return CallbackResult::Success;
  }
  CallbackResult onHeadersComplete() override { return CallbackResult::Success; }
  void bufferBody(const char*, size_t length) override { bytes_seen_ += length; }
  CallbackResult onMessageComplete() override { return CallbackResult::Success; }
  void onChunkHeader(bool) override {}

  size_t headersSeen() const { return headers_seen_; }
  size_t bytesSeen() const { return bytes_seen_; }

private:
  size_t headers_seen_{};
  size_t bytes_seen_{};
};

std::string makeHeaderName(const int index, const HeaderNameShape shape) {
  switch (shape) {
  case HeaderNameShape::LongTokenName:
    return "x-benchmark-header-" + std::to_string(index) + "-token-name";
  case HeaderNameShape::ShortName:
    return "x" + std::to_string(index);
  case HeaderNameShape::FullTokenSetName:
    return "x" + std::to_string(index) + "-" + kValidHttpTokenCharacters;
  }
  return "x-unknown";
}

std::string makeRequest(const int header_count, const HeaderNameShape shape) {
  std::string request = "GET /benchmark HTTP/1.1\r\n";
  request.reserve(32 + static_cast<size_t>(header_count) * 96);
  for (int i = 0; i < header_count; ++i) {
    request.append(makeHeaderName(i, shape));
    request.append(": value\r\n");
  }
  request.append("\r\n");
  return request;
}

void bmParseHeaders(benchmark::State& state) {
  const int header_count = state.range(0);
  const HeaderNameShape shape = static_cast<HeaderNameShape>(state.range(1));
  const std::string request = makeRequest(header_count, shape);
  state.SetLabel(headerNameShapeLabel(shape));
  {
    HeaderCountingCallbacks callbacks;
    BalsaParser parser(MessageType::Request, &callbacks, request.size(), false, false);
    const size_t parsed = parser.execute(request.data(), request.size());
    if (parsed != request.size() || parser.getStatus() != ParserStatus::Ok ||
        callbacks.headersSeen() != static_cast<size_t>(header_count)) {
      state.SkipWithError("benchmark request failed to parse");
      return;
    }
  }

  for (auto _ : state) {
    HeaderCountingCallbacks callbacks;
    BalsaParser parser(MessageType::Request, &callbacks, request.size(), false, false);
    const size_t parsed = parser.execute(request.data(), request.size());
    benchmark::DoNotOptimize(parsed);
    benchmark::DoNotOptimize(callbacks.headersSeen());
    benchmark::DoNotOptimize(callbacks.bytesSeen());
  }
  state.SetItemsProcessed(state.iterations() * header_count);
  state.SetBytesProcessed(state.iterations() * static_cast<int64_t>(request.size()));
}

BENCHMARK(bmParseHeaders)
    ->ArgsProduct({{8, 16, 64, 256, 512}, {0, 1, 2}})
    ->ArgNames({"headers", "shape"});

} // namespace
} // namespace Http1
} // namespace Http
} // namespace Envoy
