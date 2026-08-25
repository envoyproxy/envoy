// Per-request cost of the Lua HTTP filter, broken down by the work the script asks for.
//
// The scripts below are the ones from an out-of-tree extension-mechanism benchmark, so that an
// in-process number here and an end-to-end throughput number there describe the same unit of
// work. Two of them exist only to be subtracted: `kNoopBoth` prices entering and leaving the VM
// with no script work at all, and `kHeaderNoCarry` prices the same header read and write without
// carrying a value between the two handlers, which is what envoyproxy/envoy#4613 forces a real
// filter to do.
//
// Every arm rebuilds the per-stream state -- header maps and stream info -- on each iteration,
// because a benchmark that reuses them measures a second write to a warm map rather than the
// first write to a cold one, and then builds a filter over it. The `fixture` arm does the rebuild
// and stops there, so what subtracting it leaves is the filter: its construction, both handler
// entries, and the work the script asks for.

#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "envoy/extensions/filters/http/lua/v3/lua.pb.h"

#include "source/common/common/macros.h"
#include "source/common/http/header_map_impl.h"
#include "source/common/stream_info/stream_info_impl.h"
#include "source/extensions/filters/http/lua/lua_filter.h"

#include "test/common/stats/stat_test_utility.h"
#include "test/mocks/api/mocks.h"
#include "test/mocks/http/mocks.h"
#include "test/mocks/thread_local/mocks.h"
#include "test/mocks/upstream/cluster_manager.h"
#include "test/test_common/test_time.h"

#include "benchmark/benchmark.h"
#include "gmock/gmock.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace Lua {
namespace {

using testing::Invoke;
using testing::NiceMock;
using testing::Return;
using testing::ReturnRef;

// --- request shape, mirrored from the extension-mechanism benchmark's contract ---------------

constexpr absl::string_view kInHeader = "x-bench-in";
constexpr absl::string_view kHeaderValue = "bench";
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

constexpr absl::string_view kRepeatHeaderAddSameKey = R"EOF(
local REPEATS = 16

function envoy_on_request(handle)
  local h = handle:headers()
  for _ = 1, REPEATS do
    h:add("x-added", "v")
  end
end
)EOF";

constexpr absl::string_view kRepeatHeaderReplace = R"EOF(
local REPEATS = 16

function envoy_on_request(handle)
  local h = handle:headers()
  for _ = 1, REPEATS do
    h:replace("x-added", "v")
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

// Reading a carried value back is where the two channels differ most, so each read arm has a
// write-only twin: the difference between the pair is the read alone. Metadata is read a whole
// filter's struct at a time -- `dynamicMetadata():get(name)` materialises every field of that
// struct as a Lua table -- so the Set16 pair shows whether one read scales with fields the
// script never asked for.
constexpr absl::string_view kMetadataSet1Get0 = R"EOF(
function envoy_on_request(handle)
  handle:streamInfo():dynamicMetadata():set("bench", "k1", "v")
end
)EOF";

constexpr absl::string_view kMetadataSet1Get1 = R"EOF(
function envoy_on_request(handle)
  handle:streamInfo():dynamicMetadata():set("bench", "k1", "v")
end

function envoy_on_response(handle)
  local md = handle:streamInfo():dynamicMetadata():get("bench")
  local v = md and md["k1"] or ""
end
)EOF";

constexpr absl::string_view kMetadataSet16Get1 = R"EOF(
local REPEATS = 16

function envoy_on_request(handle)
  local md = handle:streamInfo():dynamicMetadata()
  for i = 1, REPEATS do
    md:set("bench", "k" .. i, "v")
  end
end

function envoy_on_response(handle)
  local md = handle:streamInfo():dynamicMetadata():get("bench")
  local v = md and md["k1"] or ""
end
)EOF";

constexpr absl::string_view kFilterStateSet1Get0 = R"EOF(
local FACTORY = "envoy.string"

function envoy_on_request(handle)
  handle:streamInfo():filterState():set("bench1", FACTORY, "v")
end
)EOF";

// Sixteen reads of one filter-state object: each one serialises the object to a std::string.
constexpr absl::string_view kFilterStateSet1Get16 = R"EOF(
local FACTORY = "envoy.string"
local REPEATS = 16

function envoy_on_request(handle)
  handle:streamInfo():filterState():set("bench1", FACTORY, "v")
end

function envoy_on_response(handle)
  local fs = handle:streamInfo():filterState()
  for _ = 1, REPEATS do
    local v = fs:get("bench1")
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

// The request every arm receives. Mirrored from the extension-mechanism harness's contract so
// that the two instruments describe the same unit of work.
using HeaderTable = std::vector<std::pair<Http::LowerCaseString, std::string>>;

const HeaderTable& requestHeaderTable() {
  CONSTRUCT_ON_FIRST_USE(HeaderTable,
                         HeaderTable{
                             {Http::LowerCaseString(":method"), "GET"},
                             {Http::LowerCaseString(":path"), "/"},
                             {Http::LowerCaseString(":authority"), "bench.local"},
                             {Http::LowerCaseString(kInHeader), std::string(kHeaderValue)},
                             {Http::LowerCaseString("x-bench-in-1"), "v1"},
                             {Http::LowerCaseString("x-bench-in-2"), "v2"},
                             {Http::LowerCaseString("x-bench-in-3"), "v3"},
                             {Http::LowerCaseString("x-bench-in-4"), "v4"},
                             {Http::LowerCaseString("x-bench-in-5"), "v5"},
                             {Http::LowerCaseString("x-bench-in-6"), "v6"},
                             {Http::LowerCaseString("x-bench-in-7"), "v7"},
                             {Http::LowerCaseString("x-bench-in-8"), "v8"},
                             {Http::LowerCaseString("cookie"), std::string(kCookieJar)},
                         });
}

// --- harness ---------------------------------------------------------------------------------

class Harness {
public:
  // Worker count the filter config is told about; it only feeds the lua.lua_vm_count gauge.
  static constexpr uint32_t kConcurrency = 1;

  Harness(absl::string_view code, bool clear_route_cache = true) {
    ON_CALL(api_, rootScope()).WillByDefault(ReturnRef(*stats_store_.rootScope()));
    ON_CALL(decoder_callbacks_, streamInfo())
        .WillByDefault(Invoke([this]() -> StreamInfo::StreamInfo& { return *stream_info_; }));
    ON_CALL(encoder_callbacks_, streamInfo())
        .WillByDefault(Invoke([this]() -> StreamInfo::StreamInfo& { return *stream_info_; }));
    // Without a default action, gmock takes its "uninteresting call" path, which builds the
    // method's name as a std::string on every call. That is pure harness cost, and profiling the
    // binary showed it dominating the mocked calls the filter makes per request.
    ON_CALL(decoder_callbacks_, mostSpecificPerFilterConfig()).WillByDefault(Return(nullptr));
    ON_CALL(encoder_callbacks_, mostSpecificPerFilterConfig()).WillByDefault(Return(nullptr));

    envoy::extensions::filters::http::lua::v3::Lua proto_config;
    proto_config.mutable_default_source_code()->set_inline_string(std::string(code));
    proto_config.mutable_clear_route_cache()->set_value(clear_route_cache);
    absl::Status creation_status = absl::OkStatus();
    config_ = std::make_shared<FilterConfig>(proto_config, tls_, cluster_manager_, api_,
                                             *stats_store_.rootScope(), "bench.", kConcurrency,
                                             creation_status);
    RELEASE_ASSERT(creation_status.ok(), std::string(creation_status.message()));
    rebuildStream();
  }

  // The per-stream state every arm rebuilds before the filter runs, and the whole of what the
  // fixture arm measures.
  void rebuildStream() {
    stream_info_ = std::make_unique<StreamInfo::StreamInfoImpl>(
        Http::Protocol::Http2, time_system_, nullptr,
        StreamInfo::FilterState::LifeSpan::FilterChain);

    // Real header maps, not the Test* wrappers: TestHeaderMapImplBase calls
    // verifyByteSizeInternalForTest() after every mutation, which walks the whole map, so a
    // benchmark built on it measures its own assertion rather than the header write.
    request_headers_ = Http::RequestHeaderMapImpl::create();
    for (const auto& header : requestHeaderTable()) {
      // The table outlives every map built from it, so the key can be referenced rather than
      // copied. addCopy() would memcpy each key and then discard the copy for any key with an
      // inline entry, which here is :method, :path and :authority.
      request_headers_->addReferenceKey(header.first, header.second);
    }
    response_headers_ = Http::ResponseHeaderMapImpl::create();
    response_headers_->setStatus("200");
    benchmark::DoNotOptimize(request_headers_);
    benchmark::DoNotOptimize(response_headers_);
  }

  // One request/response pair through a filter built for this arm's script.
  void oneRequest() {
    rebuildStream();
    Filter filter(config_, time_system_);
    filter.setDecoderFilterCallbacks(decoder_callbacks_);
    filter.setEncoderFilterCallbacks(encoder_callbacks_);
    filter.decodeHeaders(*request_headers_, true);
    filter.encodeHeaders(*response_headers_, true);
    filter.onDestroy();
  }

  // Lua-heap bytes still held after a full collection: a retention figure, not a churn figure.
  // LUA_GCCOUNT reports live bytes, so a delta taken across a running collector reads as ~0 no
  // matter how much a script allocated and freed.
  uint64_t liveLuaBytesAfterGc() {
    PerLuaCodeSetup* setup = config_->perLuaCodeSetup();
    RELEASE_ASSERT(setup != nullptr, "no default source code configured");
    setup->runtimeGC();
    return setup->runtimeBytesUsed();
  }

  PerLuaCodeSetup* setup() { return config_->perLuaCodeSetup(); }
  Http::StreamDecoderFilterCallbacks& decoderCallbacks() { return decoder_callbacks_; }
  uint64_t errors() { return counterValue("bench.lua.errors"); }
  uint64_t executions() { return counterValue("bench.lua.executions"); }

private:
  // A name no counter has is a harness bug, not a zero: counter() would create one and report it.
  uint64_t counterValue(const std::string& name) {
    Stats::CounterOptConstRef counter = stats_store_.findCounterByString(name);
    RELEASE_ASSERT(counter.has_value(), name);
    return counter->get().value();
  }

  Event::TestRealTimeSystem time_system_;
  Stats::TestUtil::TestStore stats_store_;
  NiceMock<ThreadLocal::MockInstance> tls_;
  NiceMock<Upstream::MockClusterManager> cluster_manager_;
  NiceMock<Api::MockApi> api_;
  NiceMock<Http::MockStreamDecoderFilterCallbacks> decoder_callbacks_;
  NiceMock<Http::MockStreamEncoderFilterCallbacks> encoder_callbacks_;
  std::unique_ptr<StreamInfo::StreamInfoImpl> stream_info_;
  Http::RequestHeaderMapPtr request_headers_;
  Http::ResponseHeaderMapPtr response_headers_;
  FilterConfigConstSharedPtr config_;
};

// --- benchmarks ------------------------------------------------------------------------------

// One request/response pair for a given script. The error count has to stay zero, because the
// filter fails open: a script that raises on every request looks exactly like a very fast script.
void runScript(benchmark::State& state, absl::string_view code, bool clear_route_cache) {
  Harness harness(code, clear_route_cache);
  // Warm the VM and prove the script actually runs before anything is timed.
  harness.oneRequest();
  RELEASE_ASSERT(harness.executions() > 0, "script never ran");

  for (auto _ : state) {
    harness.oneRequest();
  }

  RELEASE_ASSERT(harness.errors() == 0, "script raised");
  // The retention side of any change here: anything that keeps coroutine threads or wrappers
  // alive between requests shows up as a bigger number.
  state.counters["lua_live_bytes"] = harness.liveLuaBytesAfterGc();
}

// The per-stream rebuild with no filter at all. Subtract this from every arm below.
void BM_Fixture(benchmark::State& state) {
  Harness harness(kNoopBoth); // Configured, but never entered.
  for (auto _ : state) {
    harness.rebuildStream();
  }
}
BENCHMARK(BM_Fixture);

// `clear_route_cache` on is the filter's own default. It matters for cost: with it on, every
// *request*-path header modification calls downstreamCallbacks() twice and clearRouteCache() once,
// per modification. The delta between an arm and its NoRouteClear twin is what that costs.
#define LUA_SCRIPT_BENCHMARK_CFG(Name, Code, ClearRouteCache)                                      \
  void BM_##Name(benchmark::State& state) { runScript(state, Code, ClearRouteCache); }             \
  BENCHMARK(BM_##Name);

#define LUA_SCRIPT_BENCHMARK(Name, Code) LUA_SCRIPT_BENCHMARK_CFG(Name, Code, true)

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
LUA_SCRIPT_BENCHMARK(RepeatHeaderAddSameKey, kRepeatHeaderAddSameKey)
LUA_SCRIPT_BENCHMARK(RepeatHeaderReplace, kRepeatHeaderReplace)
LUA_SCRIPT_BENCHMARK_CFG(RepeatHeaderAddNoRouteClear, kRepeatHeaderAdd, false)
LUA_SCRIPT_BENCHMARK_CFG(RepeatHeaderReplaceNoRouteClear, kRepeatHeaderReplace, false)
LUA_SCRIPT_BENCHMARK_CFG(HeadersMetadataNoRouteClear, kHeadersMetadata, false)
LUA_SCRIPT_BENCHMARK(RepeatMetadataSet, kRepeatMetadataSet)
LUA_SCRIPT_BENCHMARK(RepeatFilterStateSet, kRepeatFilterStateSet)
LUA_SCRIPT_BENCHMARK(MetadataSet1Get0, kMetadataSet1Get0)
LUA_SCRIPT_BENCHMARK(MetadataSet1Get1, kMetadataSet1Get1)
LUA_SCRIPT_BENCHMARK(MetadataSet16Get1, kMetadataSet16Get1)
LUA_SCRIPT_BENCHMARK(FilterStateSet1Get0, kFilterStateSet1Get0)
LUA_SCRIPT_BENCHMARK(FilterStateSet1Get16, kFilterStateSet1Get16)

// gmock dispatch is not free, and it is inside every filter arm but not inside BM_Fixture: the
// filter resolves its per-route config once per direction and asks for the stream info once per
// handler that touches it. These two arms price one call of each so those can be subtracted from
// an absolute figure. They say nothing about Envoy.
void BM_MockPerFilterConfigCall(benchmark::State& state) {
  Harness harness(kNoopBoth);
  Http::StreamDecoderFilterCallbacks& callbacks = harness.decoderCallbacks();
  for (auto _ : state) {
    benchmark::DoNotOptimize(callbacks.mostSpecificPerFilterConfig());
  }
}
BENCHMARK(BM_MockPerFilterConfigCall);

void BM_MockStreamInfoCall(benchmark::State& state) {
  Harness harness(kNoopBoth);
  Http::StreamDecoderFilterCallbacks& callbacks = harness.decoderCallbacks();
  for (auto _ : state) {
    benchmark::DoNotOptimize(&callbacks.streamInfo());
  }
}
BENCHMARK(BM_MockStreamInfoCall);

// Coroutine creation and teardown on its own, with no filter, no wrapper and no script entry.
// This is the allocation the filter makes once per direction per request.
void BM_CreateCoroutine(benchmark::State& state) {
  Harness harness(kNoopBoth);
  PerLuaCodeSetup* setup = harness.setup();
  for (auto _ : state) {
    Filters::Common::Lua::CoroutinePtr coroutine = setup->createCoroutine();
    benchmark::DoNotOptimize(coroutine);
  }

  state.counters["lua_live_bytes"] = harness.liveLuaBytesAfterGc();
}
BENCHMARK(BM_CreateCoroutine);

} // namespace
} // namespace Lua
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
