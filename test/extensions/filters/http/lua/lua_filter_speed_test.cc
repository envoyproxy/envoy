// Per-request cost of the Lua HTTP filter, broken down by the work the script asks for.
//
// The scripts below are the ones from an out-of-tree extension-mechanism benchmark, so that an
// in-process number here and an end-to-end throughput number there describe the same unit of
// work. Two of them exist only to be subtracted: `kNoopBoth` prices entering and leaving the VM
// with no script work at all, and `kHeaderNoCarry` prices the same header read and write without
// carrying a value between the two handlers, which is what envoyproxy/envoy#4613 forces a real
// filter to do.
//
// Every arm rebuilds the per-stream state (filter, headers, stream info) on each iteration,
// because a benchmark that reuses them measures a second write to a warm map rather than the
// first write to a cold one. The `fixture` arm does that rebuild and nothing else, so the
// rebuild cost can be subtracted rather than argued about.

#include <memory>
#include <string>

#include "envoy/extensions/filters/http/lua/v3/lua.pb.h"

#include "source/common/stream_info/stream_info_impl.h"
#include "source/extensions/filters/http/lua/lua_filter.h"

#include "test/mocks/api/mocks.h"
#include "test/mocks/http/mocks.h"
#include "test/mocks/thread_local/mocks.h"
#include "test/mocks/upstream/cluster_manager.h"
#include "test/test_common/test_time.h"
#include "test/test_common/utility.h"

#include "benchmark/benchmark.h"
#include "gmock/gmock.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace Lua {
namespace {

using testing::Invoke;
using testing::NiceMock;
using testing::ReturnRef;

// --- request shape, mirrored from the extension-mechanism benchmark's contract ---------------

constexpr absl::string_view kInHeader = "x-bench-in";
constexpr absl::string_view kHeaderValue = "bench";
constexpr int kFanout = 8;
constexpr absl::string_view kCookieJar =
    "_csrf_token=hp8Kk3nQ; jitney_client_session_id=6f2c1a0e-9b1d; _aat=0%2FGa; "
    "bev=1755820000_YmM0ZTgz; _airbed_session_id=9c1f4b2e7a; li=0; frmfctr=wide; "
    "everest_cookie=1755820000.EAJ; cdn_exp_9f0=treatment; _uc=1";

// --- scripts ---------------------------------------------------------------------------------

// Both handlers present, neither doing anything: two coroutines, two stream handles, two VM
// entries, no script work.
constexpr absl::string_view kNoopBoth = R"EOF(
function envoy_on_request(handle) end
function envoy_on_response(handle) end
)EOF";

// One handler present. The difference against kNoopBoth is one coroutine, one stream handle and
// one VM entry.
constexpr absl::string_view kNoopRequestOnly = R"EOF(
function envoy_on_request(handle) end
)EOF";

constexpr absl::string_view kNoopResponseOnly = R"EOF(
function envoy_on_response(handle) end
)EOF";

// Read one request header, write one response header from it, carried through dynamic metadata.
constexpr absl::string_view kHeaderMetadata = R"EOF(
function envoy_on_request(handle)
  local v = handle:headers():get("x-bench-in")
  handle:streamInfo():dynamicMetadata():set("bench", "in", v or "")
end

function envoy_on_response(handle)
  local md = handle:streamInfo():dynamicMetadata():get("bench")
  local v = md and md["in"] or ""
  handle:headers():replace("x-bench-out", v)
end
)EOF";

// The same, carried through filter state.
constexpr absl::string_view kHeaderFilterState = R"EOF(
local FACTORY = "envoy.string"

function envoy_on_request(handle)
  local v = handle:headers():get("x-bench-in") or ""
  handle:streamInfo():filterState():set("bench", FACTORY, v)
end

function envoy_on_response(handle)
  local v = handle:streamInfo():filterState():get("bench")
  handle:headers():replace("x-bench-out", v or "")
end
)EOF";

// The same header read and the same header write, with no carry between the handlers. Not
// equivalent work -- the response handler writes a literal -- but it is the floor the two carry
// channels are measured against.
constexpr absl::string_view kHeaderNoCarry = R"EOF(
function envoy_on_request(handle)
  local v = handle:headers():get("x-bench-in")
end

function envoy_on_response(handle)
  handle:headers():replace("x-bench-out", "bench")
end
)EOF";

// Eight reads and eight writes, one carry.
constexpr absl::string_view kHeadersMetadata = R"EOF(
local FANOUT = 8

function envoy_on_request(handle)
  local h = handle:headers()
  local carried = {}
  for i = 1, FANOUT do
    carried["v" .. i] = h:get("x-bench-in-" .. i) or ""
  end
  handle:streamInfo():dynamicMetadata():set("bench", "in", carried)
end

function envoy_on_response(handle)
  local md = handle:streamInfo():dynamicMetadata():get("bench")
  local carried = md and md["in"] or {}
  local h = handle:headers()
  for i = 1, FANOUT do
    h:replace("x-bench-out-" .. i, carried["v" .. i] or "")
  end
  h:replace("x-bench-out", tostring(FANOUT))
end
)EOF";

// One header read, then a real scan: parse a ten-pair cookie jar for one name.
constexpr absl::string_view kCookieMetadata = R"EOF(
local WANTED = "bev"

local function cookie_value(jar, name)
  for k, v in string.gmatch(jar, "([^=;%s]+)=([^;]*)") do
    if k == name then
      return v
    end
  end
  return ""
end

function envoy_on_request(handle)
  local jar = handle:headers():get("cookie") or ""
  handle:streamInfo():dynamicMetadata():set("bench", "in", cookie_value(jar, WANTED))
end

function envoy_on_response(handle)
  local md = handle:streamInfo():dynamicMetadata():get("bench")
  handle:headers():replace("x-bench-out", md and md["in"] or "")
end
)EOF";

// Marginal cost of one wrapper accessor call. Each arm calls the accessor a fixed extra number
// of times on an otherwise identical request, so the per-call cost is the delta divided by the
// extra count rather than anything that needs the fixture subtracted out.
constexpr absl::string_view kRepeatHeadersAccessor = R"EOF(
local REPEATS = 16

function envoy_on_request(handle)
  for _ = 1, REPEATS do
    local h = handle:headers()
  end
end
)EOF";

constexpr absl::string_view kRepeatHeaderGet = R"EOF(
local REPEATS = 16

function envoy_on_request(handle)
  local h = handle:headers()
  for _ = 1, REPEATS do
    local v = h:get("x-bench-in")
  end
end
)EOF";

constexpr absl::string_view kRepeatHeaderGetLongKey = R"EOF(
local REPEATS = 16

function envoy_on_request(handle)
  local h = handle:headers()
  for _ = 1, REPEATS do
    local v = h:get("x-bench-in-with-a-deliberately-long-header-name")
  end
end
)EOF";

constexpr absl::string_view kRepeatHeaderAdd = R"EOF(
local REPEATS = 16

function envoy_on_request(handle)
  local h = handle:headers()
  for i = 1, REPEATS do
    h:add("x-added-" .. i, "v")
  end
end
)EOF";

constexpr absl::string_view kRepeatMetadataSet = R"EOF(
local REPEATS = 16

function envoy_on_request(handle)
  local md = handle:streamInfo():dynamicMetadata()
  for i = 1, REPEATS do
    md:set("bench", "k" .. i, "v")
  end
end
)EOF";

constexpr absl::string_view kRepeatFilterStateSet = R"EOF(
local REPEATS = 16
local FACTORY = "envoy.string"

function envoy_on_request(handle)
  local fs = handle:streamInfo():filterState()
  for i = 1, REPEATS do
    fs:set("bench" .. i, FACTORY, "v")
  end
end
)EOF";

// The empty-loop control for the four kRepeat* arms above: the same 16 Lua iterations with no
// Envoy call in them, so that the loop itself is not attributed to the accessor.
constexpr absl::string_view kRepeatNothing = R"EOF(
local REPEATS = 16

function envoy_on_request(handle)
  local n = 0
  for _ = 1, REPEATS do
    n = n + 1
  end
end
)EOF";

// --- harness ---------------------------------------------------------------------------------

class Harness {
public:
  Harness(absl::string_view code, uint32_t concurrency = 1) {
    ON_CALL(api_, rootScope()).WillByDefault(ReturnRef(*stats_store_.rootScope()));
    ON_CALL(decoder_callbacks_, streamInfo()).WillByDefault(Invoke([this]() -> StreamInfo::StreamInfo& {
      return *stream_info_;
    }));
    ON_CALL(encoder_callbacks_, streamInfo()).WillByDefault(Invoke([this]() -> StreamInfo::StreamInfo& {
      return *stream_info_;
    }));

    if (!code.empty()) {
      envoy::extensions::filters::http::lua::v3::Lua proto_config;
      proto_config.mutable_default_source_code()->set_inline_string(std::string(code));
      absl::Status creation_status = absl::OkStatus();
      config_ = std::make_shared<FilterConfig>(proto_config, tls_, cluster_manager_, api_,
                                               *stats_store_.rootScope(), "bench.", concurrency,
                                               creation_status);
      RELEASE_ASSERT(creation_status.ok(), std::string(creation_status.message()));
    }
  }

  // Rebuild the per-stream state and, if a script is configured, run one request/response pair
  // through the filter.
  void oneRequest() {
    stream_info_ = std::make_unique<StreamInfo::StreamInfoImpl>(
        Http::Protocol::Http2, time_system_.timeSystem(), nullptr,
        StreamInfo::FilterState::LifeSpan::FilterChain);

    Http::TestRequestHeaderMapImpl request_headers{
        {":method", "GET"},
        {":path", "/"},
        {":authority", "bench.local"},
        {std::string(kInHeader), std::string(kHeaderValue)},
        {"x-bench-in-1", "v1"},
        {"x-bench-in-2", "v2"},
        {"x-bench-in-3", "v3"},
        {"x-bench-in-4", "v4"},
        {"x-bench-in-5", "v5"},
        {"x-bench-in-6", "v6"},
        {"x-bench-in-7", "v7"},
        {"x-bench-in-8", "v8"},
        {"cookie", std::string(kCookieJar)},
    };
    Http::TestResponseHeaderMapImpl response_headers{{":status", "200"}};

    if (config_ == nullptr) {
      benchmark::DoNotOptimize(request_headers);
      benchmark::DoNotOptimize(response_headers);
      return;
    }

    Filter filter(config_, time_system_.timeSystem());
    filter.setDecoderFilterCallbacks(decoder_callbacks_);
    filter.setEncoderFilterCallbacks(encoder_callbacks_);
    filter.decodeHeaders(request_headers, true);
    filter.encodeHeaders(response_headers, true);
    filter.onDestroy();
    benchmark::DoNotOptimize(response_headers);
  }

  // Bytes the Lua runtime currently holds. Sampled outside the timed loop, so the delta over a
  // run is the Lua-heap churn the script caused, which is what the collector later has to walk.
  uint64_t luaBytesUsed() {
    PerLuaCodeSetup* setup = config_->perLuaCodeSetup();
    RELEASE_ASSERT(setup != nullptr, "no default source code configured");
    return setup->runtimeBytesUsed();
  }

  void collectLuaGarbage() { config_->perLuaCodeSetup()->runtimeGC(); }

  PerLuaCodeSetup* setup() { return config_->perLuaCodeSetup(); }
  uint64_t errors() {
    return stats_store_.counter("bench.lua.errors").value();
  }
  uint64_t executions() { return stats_store_.counter("bench.lua.executions").value(); }

private:
  Event::TestRealTimeSystem time_system_;
  Stats::IsolatedStoreImpl stats_store_;
  NiceMock<ThreadLocal::MockInstance> tls_;
  NiceMock<Upstream::MockClusterManager> cluster_manager_;
  NiceMock<Api::MockApi> api_;
  NiceMock<Http::MockStreamDecoderFilterCallbacks> decoder_callbacks_;
  NiceMock<Http::MockStreamEncoderFilterCallbacks> encoder_callbacks_;
  std::unique_ptr<StreamInfo::StreamInfoImpl> stream_info_;
  FilterConfigConstSharedPtr config_;
};

// --- benchmarks ------------------------------------------------------------------------------

// One request/response pair for a given script. `lua_bytes/req` is Lua-heap bytes allocated per
// request; `errors` must stay zero, because the filter fails open and a script that raises on
// every request looks exactly like a fast script.
void runScript(benchmark::State& state, absl::string_view code) {
  Harness harness(code);
  // Warm the VM and prove the script actually runs before anything is timed.
  harness.oneRequest();
  RELEASE_ASSERT(harness.errors() == 0, "script raised");
  RELEASE_ASSERT(harness.executions() > 0, "script never ran");

  harness.collectLuaGarbage();
  const uint64_t bytes_before = harness.luaBytesUsed();
  uint64_t iterations = 0;
  for (auto _ : state) {
    harness.oneRequest();
    ++iterations;
  }
  state.PauseTiming();
  const uint64_t bytes_after = harness.luaBytesUsed();
  state.counters["lua_bytes/req"] =
      iterations == 0 ? 0 : static_cast<double>(bytes_after - bytes_before) / iterations;
  state.counters["errors"] = harness.errors();
  state.ResumeTiming();
}

// The per-stream rebuild with no filter at all. Subtract this from every arm above.
void BM_Fixture(benchmark::State& state) {
  Harness harness("");
  for (auto _ : state) {
    harness.oneRequest();
  }
}
BENCHMARK(BM_Fixture);

#define LUA_SCRIPT_BENCHMARK(Name, Code)                                                           \
  void BM_##Name(benchmark::State& state) { runScript(state, Code); }                               \
  BENCHMARK(BM_##Name);

LUA_SCRIPT_BENCHMARK(NoopBoth, kNoopBoth)
LUA_SCRIPT_BENCHMARK(NoopRequestOnly, kNoopRequestOnly)
LUA_SCRIPT_BENCHMARK(NoopResponseOnly, kNoopResponseOnly)
LUA_SCRIPT_BENCHMARK(HeaderNoCarry, kHeaderNoCarry)
LUA_SCRIPT_BENCHMARK(HeaderMetadata, kHeaderMetadata)
LUA_SCRIPT_BENCHMARK(HeaderFilterState, kHeaderFilterState)
LUA_SCRIPT_BENCHMARK(HeadersMetadata, kHeadersMetadata)
LUA_SCRIPT_BENCHMARK(CookieMetadata, kCookieMetadata)
LUA_SCRIPT_BENCHMARK(RepeatNothing, kRepeatNothing)
LUA_SCRIPT_BENCHMARK(RepeatHeadersAccessor, kRepeatHeadersAccessor)
LUA_SCRIPT_BENCHMARK(RepeatHeaderGet, kRepeatHeaderGet)
LUA_SCRIPT_BENCHMARK(RepeatHeaderGetLongKey, kRepeatHeaderGetLongKey)
LUA_SCRIPT_BENCHMARK(RepeatHeaderAdd, kRepeatHeaderAdd)
LUA_SCRIPT_BENCHMARK(RepeatMetadataSet, kRepeatMetadataSet)
LUA_SCRIPT_BENCHMARK(RepeatFilterStateSet, kRepeatFilterStateSet)

// Coroutine creation and teardown on its own, with no filter, no wrapper and no script entry.
// This is the allocation the filter makes once per direction per request.
void BM_CreateCoroutine(benchmark::State& state) {
  Harness harness(kNoopBoth);
  PerLuaCodeSetup* setup = harness.setup();
  harness.collectLuaGarbage();
  const uint64_t bytes_before = harness.luaBytesUsed();
  uint64_t iterations = 0;
  for (auto _ : state) {
    Filters::Common::Lua::CoroutinePtr coroutine = setup->createCoroutine();
    benchmark::DoNotOptimize(coroutine);
    ++iterations;
  }
  state.PauseTiming();
  const uint64_t bytes_after = harness.luaBytesUsed();
  state.counters["lua_bytes/coroutine"] =
      iterations == 0 ? 0 : static_cast<double>(bytes_after - bytes_before) / iterations;
  state.ResumeTiming();
}
BENCHMARK(BM_CreateCoroutine);

} // namespace
} // namespace Lua
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
