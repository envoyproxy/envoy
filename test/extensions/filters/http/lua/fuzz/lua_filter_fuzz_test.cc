#include <deque>
#include <memory>
#include <string>
#include <vector>

#include "source/common/http/message_impl.h"
#include "source/common/http/utility.h"
#include "source/common/stats/isolated_store_impl.h"
#include "source/extensions/filters/http/lua/lua_filter.h"

#include "test/extensions/filters/http/lua/fuzz/lua_filter_fuzz.pb.validate.h"
#include "test/fuzz/fuzz_runner.h"
#include "test/fuzz/utility.h"
#include "test/mocks/api/mocks.h"
#include "test/mocks/http/mocks.h"
#include "test/mocks/server/factory_context.h"
#include "test/mocks/thread_local/mocks.h"
#include "test/mocks/upstream/cluster_manager.h"
#include "test/test_common/simulated_time_system.h"
#include "test/test_common/utility.h"

using testing::_;
using testing::NiceMock;
using testing::ReturnRef;

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace Lua {
namespace {

using FuzzCase = test::extensions::filters::http::lua::LuaFilterFuzzTestCase;
using HttpCallResponse = test::extensions::filters::http::lua::HttpCallResponse;

// The only cluster a script's httpCall() can name.
constexpr absl::string_view HttpCallCluster = "cluster";

// Every script reads its arguments from x-fuzz-* request headers, so a mutation of the request is
// a mutation of what the bindings are handed. None loops other than over a binding's own iterator,
// so no input can hang the script.
constexpr absl::string_view HeadersScript = R"EOF(
local function mutate(headers)
  local name = headers:get("x-fuzz-name")
  local value = headers:get("x-fuzz-value") or ""
  if name then
    headers:add(name, value)
    headers:getAtIndex(name, headers:getNumValues(name) - 1)
    headers:replace(name, value)
    headers:remove(name)
  end
  local count = 0
  for _, _ in pairs(headers) do
    count = count + 1
  end
  headers:replace("x-fuzz-count", tostring(count))
end

function envoy_on_request(handle)
  mutate(handle:headers())
  local trailers = handle:trailers()
  if trailers then
    mutate(trailers)
  end
end

function envoy_on_response(handle)
  local headers = handle:headers()
  mutate(headers)
  headers:setHttp1ReasonPhrase(handle:headers():get("x-fuzz-reason") or "OK")
end
)EOF";

constexpr absl::string_view RespondScript = R"EOF(
function envoy_on_request(handle)
  local headers = handle:headers()
  local reply = { [":status"] = headers:get("x-fuzz-status") or "200" }
  local name = headers:get("x-fuzz-name")
  if name then
    reply[name] = { headers:get("x-fuzz-value") or "", headers:getAtIndex("x-fuzz-value", 1) or "" }
  end
  handle:respond(reply, headers:get("x-fuzz-body"))
end
)EOF";

constexpr absl::string_view BodyScript = R"EOF(
local function rewrite(handle, mode, index, length, value)
  if mode == "chunks" then
    for chunk in handle:bodyChunks() do
      chunk:getBytes(0, chunk:length())
      chunk:setBytes(value)
    end
    return
  end
  local body = handle:body(mode == "always")
  if body then
    body:getBytes(tonumber(index) or 0, tonumber(length) or body:length())
    body:setBytes(value)
  end
end

function envoy_on_request(handle)
  local headers = handle:headers()
  rewrite(handle, headers:get("x-fuzz-mode"), headers:get("x-fuzz-index"),
          headers:get("x-fuzz-length"), headers:get("x-fuzz-value") or "")
end

function envoy_on_response(handle)
  local headers = handle:headers()
  rewrite(handle, headers:get("x-fuzz-mode"), headers:get("x-fuzz-index"),
          headers:get("x-fuzz-length"), headers:get("x-fuzz-value") or "")
end
)EOF";

constexpr absl::string_view HttpCallScript = R"EOF(
function envoy_on_request(handle)
  local headers = handle:headers()
  local call = {
    [":method"] = headers:get("x-fuzz-method") or "GET",
    [":path"] = headers:get("x-fuzz-path") or "/",
    [":authority"] = headers:get("x-fuzz-authority") or "fuzz",
  }
  local name = headers:get("x-fuzz-name")
  if name then
    call[name] = headers:get("x-fuzz-value") or ""
  end
  local response_headers, response_body = handle:httpCall(
    "cluster", call, headers:get("x-fuzz-body"),
    tonumber(headers:get("x-fuzz-timeout") or "100") or 100,
    headers:get("x-fuzz-async") == "true")
  if response_headers then
    -- The yield invalidated every wrapper the script held, so the headers are fetched again.
    headers = handle:headers()
    headers:replace("x-fuzz-call-status", response_headers[":status"] or "")
    headers:replace("x-fuzz-call-length", tostring(response_body and #response_body or 0))
    if headers:get("x-fuzz-relay") then
      handle:respond(response_headers, response_body)
    end
  end
end
)EOF";

constexpr absl::string_view StreamInfoScript = R"EOF(
function envoy_on_request(handle)
  local headers = handle:headers()
  local name = headers:get("x-fuzz-name") or "fuzz"
  local value = headers:get("x-fuzz-value") or ""
  local metadata = handle:streamInfo():dynamicMetadata()
  metadata:set(name, "value", value)
  metadata:get(name)
  handle:streamInfo():filterState():get(value)
  handle:streamInfo():dynamicTypedMetadata(name)
  handle:metadata():get(value)
  handle:base64Decode(handle:base64Escape(value))
  handle:base64Decode(value)
  handle:timestampString(tonumber(headers:get("x-fuzz-index") or "0") or 0)
  handle:stats():counter("fuzz"):add(tonumber(headers:get("x-fuzz-length") or "1") or 1)
end

function envoy_on_response(handle)
  local metadata = handle:streamInfo():dynamicMetadata():get(
    handle:headers():get("x-fuzz-name") or "fuzz")
  if metadata then
    handle:headers():replace("x-fuzz-value", tostring(metadata.value))
  end
end
)EOF";

absl::string_view scriptFor(FuzzCase::Script script) {
  switch (script) {
    PANIC_ON_PROTO_ENUM_SENTINEL_VALUES;
  case FuzzCase::HEADERS:
    return HeadersScript;
  case FuzzCase::RESPOND:
    return RespondScript;
  case FuzzCase::BODY:
    return BodyScript;
  case FuzzCase::HTTP_CALL:
    return HttpCallScript;
  case FuzzCase::STREAM_INFO:
    return StreamInfoScript;
  }
  PANIC_DUE_TO_CORRUPT_ENUM;
}

// What a filter config needs and outlives every stream. Built once, because the cluster manager
// and the server context are the expensive mocks.
class ConfigMocks {
public:
  ConfigMocks() {
    cluster_manager_.initializeThreadLocalClusters({std::string(HttpCallCluster)});
    ON_CALL(api_, rootScope()).WillByDefault(ReturnRef(*stats_store_.rootScope()));
    ON_CALL(server_factory_context_.api_, rootScope())
        .WillByDefault(ReturnRef(*stats_store_.rootScope()));
  }

  Stats::IsolatedStoreImpl stats_store_;
  NiceMock<ThreadLocal::MockInstance> tls_;
  NiceMock<Api::MockApi> api_;
  NiceMock<Upstream::MockClusterManager> cluster_manager_;
  NiceMock<Server::Configuration::MockServerFactoryContext> server_factory_context_;
  Event::SimulatedTimeSystem time_system_;
};

// Drives one stream through the filter, answering each httpCall() as soon as the filter call that
// made it returns, and stopping at a local reply as the connection manager would.
class StreamDriver {
public:
  StreamDriver(ConfigMocks& mocks, const FuzzCase& input)
      : mocks_(mocks), input_(input),
        request_(&mocks.cluster_manager_.thread_local_cluster_.async_client_) {
    ON_CALL(decoder_callbacks_, addDecodedData(_, _))
        .WillByDefault(
            [this](Buffer::Instance& data, bool) { append(decoder_callbacks_.buffer_, data); });
    ON_CALL(encoder_callbacks_, addEncodedData(_, _))
        .WillByDefault(
            [this](Buffer::Instance& data, bool) { append(encoder_callbacks_.buffer_, data); });
    ON_CALL(decoder_callbacks_, sendLocalReply(_, _, _, _, _)).WillByDefault([this] {
      replied_ = true;
    });
    ON_CALL(mocks_.cluster_manager_.thread_local_cluster_.async_client_, send_(_, _, _))
        .WillByDefault(
            [this](Http::RequestMessagePtr&, Http::AsyncClient::Callbacks& callbacks,
                   const Http::AsyncClient::RequestOptions&) -> Http::AsyncClient::Request* {
              pending_.push_back(&callbacks);
              return &request_;
            });
  }

  void run() {
    envoy::extensions::filters::http::lua::v3::Lua proto_config;
    proto_config.mutable_default_source_code()->set_inline_string(scriptFor(input_.script()));
    absl::Status creation_status = absl::OkStatus();
    auto config = std::make_shared<FilterConfig>(
        proto_config, mocks_.tls_, mocks_.cluster_manager_, mocks_.api_,
        *mocks_.stats_store_.rootScope(), "fuzz.",
        mocks_.server_factory_context_.options().concurrency(),
        mocks_.server_factory_context_.singletonManager(), creation_status);
    RELEASE_ASSERT(creation_status.ok(), std::string(creation_status.message()));

    Filter filter(config, mocks_.time_system_);
    filter.setDecoderFilterCallbacks(decoder_callbacks_);
    filter.setEncoderFilterCallbacks(encoder_callbacks_);

    decode(filter);
    if (!replied_ && input_.has_response()) {
      encode(filter);
    }
    filter.onDestroy();
  }

private:
  static void append(Buffer::InstancePtr& buffer, Buffer::Instance& data) {
    if (buffer == nullptr) {
      buffer = std::make_unique<Buffer::OwnedImpl>();
    }
    buffer->move(data);
  }

  void decode(Filter& filter) {
    const test::fuzz::HttpData& data = input_.request();
    request_headers_ = Fuzz::fromHeaders<Http::TestRequestHeaderMapImpl>(data.headers());
    if (request_headers_.getPathValue().empty()) {
      request_headers_.setPath("/");
    }
    if (request_headers_.getMethodValue().empty()) {
      request_headers_.setMethod("GET");
    }
    if (request_headers_.getHostValue().empty()) {
      request_headers_.setHost("fuzz");
    }

    const std::vector<std::string> chunks = Fuzz::parseHttpData(data);
    filter.decodeHeaders(request_headers_, chunks.empty() && !data.has_trailers());
    answerCalls();
    for (size_t i = 0; i < chunks.size() && !replied_; ++i) {
      Buffer::OwnedImpl buffer(chunks[i]);
      filter.decodeData(buffer, i + 1 == chunks.size() && !data.has_trailers());
      answerCalls();
    }
    if (data.has_trailers() && !replied_) {
      request_trailers_ = Fuzz::fromHeaders<Http::TestRequestTrailerMapImpl>(data.trailers());
      filter.decodeTrailers(request_trailers_);
      answerCalls();
    }
  }

  void encode(Filter& filter) {
    const test::fuzz::HttpData& data = input_.response();
    response_headers_ = Fuzz::fromHeaders<Http::TestResponseHeaderMapImpl>(data.headers());
    if (!Http::Utility::getResponseStatusOrNullopt(response_headers_).has_value()) {
      response_headers_.setStatus(200);
    }

    const std::vector<std::string> chunks = Fuzz::parseHttpData(data);
    filter.encodeHeaders(response_headers_, chunks.empty() && !data.has_trailers());
    answerCalls();
    for (size_t i = 0; i < chunks.size(); ++i) {
      Buffer::OwnedImpl buffer(chunks[i]);
      filter.encodeData(buffer, i + 1 == chunks.size() && !data.has_trailers());
      answerCalls();
    }
    if (data.has_trailers()) {
      response_trailers_ = Fuzz::fromHeaders<Http::TestResponseTrailerMapImpl>(data.trailers());
      filter.encodeTrailers(response_trailers_);
      answerCalls();
    }
  }

  void answerCalls() {
    while (!pending_.empty()) {
      Http::AsyncClient::Callbacks* callbacks = pending_.front();
      pending_.pop_front();
      if (next_response_ == input_.http_call_responses_size()) {
        callbacks->onFailure(request_, Http::AsyncClient::FailureReason::Reset);
        continue;
      }
      const HttpCallResponse& response = input_.http_call_responses(next_response_++);
      auto headers = std::make_unique<Http::TestResponseHeaderMapImpl>(
          Fuzz::fromHeaders<Http::TestResponseHeaderMapImpl>(response.headers()));
      if (!Http::Utility::getResponseStatusOrNullopt(*headers).has_value()) {
        headers->setStatus(200);
      }
      auto message = std::make_unique<Http::ResponseMessageImpl>(std::move(headers));
      message->body().add(response.body());
      callbacks->onSuccess(request_, std::move(message));
    }
  }

  ConfigMocks& mocks_;
  const FuzzCase& input_;
  NiceMock<Http::MockStreamDecoderFilterCallbacks> decoder_callbacks_;
  NiceMock<Http::MockStreamEncoderFilterCallbacks> encoder_callbacks_;
  NiceMock<Http::MockAsyncClientRequest> request_;
  std::deque<Http::AsyncClient::Callbacks*> pending_;
  int next_response_{0};
  bool replied_{false};
  Http::TestRequestHeaderMapImpl request_headers_;
  Http::TestRequestTrailerMapImpl request_trailers_;
  Http::TestResponseHeaderMapImpl response_headers_;
  Http::TestResponseTrailerMapImpl response_trailers_;
};

} // namespace

DEFINE_PROTO_FUZZER(const FuzzCase& input) {
  try {
    TestUtility::validate(input);
  } catch (const EnvoyException& e) {
    ENVOY_LOG_MISC(debug, "EnvoyException during validation: {}", e.what());
    return;
  }

  static ConfigMocks mocks;
  StreamDriver(mocks, input).run();
}

} // namespace Lua
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
