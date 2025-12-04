#include "source/extensions/filters/http/lua/lua_filter.h"

#include <lua.h>

#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>

#include "envoy/http/codes.h"

#include "source/common/buffer/buffer_impl.h"
#include "source/common/common/assert.h"
#include "source/common/common/enum_to_int.h"
#include "source/common/config/datasource.h"
#include "source/common/crypto/crypto_impl.h"
#include "source/common/crypto/utility.h"
#include "source/common/http/message_impl.h"

#include "absl/strings/escaping.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace Lua {

namespace {

using OptionHandler =
    std::function<void(lua_State* state, StreamHandleWrapper::HttpCallOptions& options)>;
using OptionHandlers = std::map<absl::string_view, OptionHandler>;

const OptionHandlers& optionHandlers() {
  CONSTRUCT_ON_FIRST_USE(OptionHandlers,
                         {
                             {"asynchronous",
                              [](lua_State* state, StreamHandleWrapper::HttpCallOptions& options) {
                                // Handle the case when the table has: {["asynchronous"] =
                                // <boolean>} entry.
                                options.is_async_request_ = lua_toboolean(state, -1);
                              }},
                             {"timeout_ms",
                              [](lua_State* state, StreamHandleWrapper::HttpCallOptions& options) {
                                // Handle the case when the table has: {["timeout_ms"] = <int>}
                                // entry.
                                const int timeout_ms = luaL_checkint(state, -1);
                                if (timeout_ms < 0) {
                                  luaL_error(state, "http call timeout must be >= 0");
                                } else {
                                  options.request_options_.setTimeout(
                                      std::chrono::milliseconds(timeout_ms));
                                }
                              }},
                             {"trace_sampled",
                              [](lua_State* state, StreamHandleWrapper::HttpCallOptions& options) {
                                const bool sampled = lua_toboolean(state, -1);
                                options.request_options_.setSampled(sampled);
                              }},
                             {"return_duplicate_headers",
                              [](lua_State* state, StreamHandleWrapper::HttpCallOptions& options) {
                                // Handle the case when the table has: {["return_duplicate_headers"]
                                // = <boolean>} entry.
                                options.return_duplicate_headers_ = lua_toboolean(state, -1);
                              }},
                             {"send_xff",
                              [](lua_State* state, StreamHandleWrapper::HttpCallOptions& options) {
                                // Handle the case when the table has: {["send_xff"] =
                                // <boolean>} entry.
                                options.request_options_.setSendXff(lua_toboolean(state, -1));
                              }},
                         });
}

constexpr int AsyncFlagIndex = 6;
constexpr int HttpCallOptionsIndex = 5;

struct HttpResponseCodeDetailValues {
  const absl::string_view LuaResponse = "lua_response";
};
using HttpResponseCodeDetails = ConstSingleton<HttpResponseCodeDetailValues>;

// Parse http call options by inspecting the provided table.
void parseOptionsFromTable(lua_State* state, int index,
                           StreamHandleWrapper::HttpCallOptions& options) {
  const auto& handlers = optionHandlers();

  lua_pushnil(state);
  while (lua_next(state, index) != 0) {
    // Uses 'key' (at index -2) and 'value' (at index -1). We only care for string keys.
    size_t key_length = 0;
    const char* key = luaL_checklstring(state, -2, &key_length);
    if (key == nullptr) {
      continue;
    }

    auto iter = handlers.find(absl::string_view(key, key_length));
    if (iter != handlers.end()) {
      iter->second(state, options);
    } else {
      luaL_error(state, "\"%s\" is not valid key for httpCall() options", key);
    }

    // Pop value of key/value pair.
    lua_pop(state, 1);
  }
}

const Protobuf::Struct& getMetadata(Http::StreamFilterCallbacks* callbacks) {
  if (callbacks->route() == nullptr) {
    return Protobuf::Struct::default_instance();
  }
  const auto& metadata = callbacks->route()->metadata();

  {
    auto filter_it = metadata.filter_metadata().find(callbacks->filterConfigName());
    if (filter_it != metadata.filter_metadata().end()) {
      return filter_it->second;
    }
    // TODO(wbpcode): deprecate this in favor of the above.
    filter_it = metadata.filter_metadata().find("envoy.filters.http.lua");
    if (filter_it != metadata.filter_metadata().end()) {
      return filter_it->second;
    }
  }

  return Protobuf::Struct::default_instance();
}

// Okay to return non-const reference because this doesn't ever get changed.
NoopCallbacks& noopCallbacks() {
  static NoopCallbacks* callbacks = new NoopCallbacks();
  return *callbacks;
}

void buildHeadersFromTable(Http::HeaderMap& headers, lua_State* state, int table_index) {
  // Build a header map to make the request. We iterate through the provided table to do this and
  // check that we are getting strings.
  lua_pushnil(state);
  while (lua_next(state, table_index) != 0) {
    // Uses 'key' (at index -2) and 'value' (at index -1).
    const char* key = luaL_checkstring(state, -2);
    // Check if the current value is a table, we iterate through the table and add each element of
    // it as a header entry value for the current key.
    if (lua_istable(state, -1)) {
      lua_pushnil(state);
      while (lua_next(state, -2) != 0) {
        const char* value = luaL_checkstring(state, -1);
        headers.addCopy(Http::LowerCaseString(key), value);
        lua_pop(state, 1);
      }
    } else {
      const char* value = luaL_checkstring(state, -1);
      headers.addCopy(Http::LowerCaseString(key), value);
    }
    // Removes 'value'; keeps 'key' for next iteration. This is the input for lua_next() so that
    // it can push the next key/value pair onto the stack.
    lua_pop(state, 1);
  }
}

Http::AsyncClient::Request* makeHttpCall(lua_State* state, Filter& filter,
                                         const Http::AsyncClient::RequestOptions& options,
                                         Http::AsyncClient::Callbacks& callbacks) {
  const std::string cluster = luaL_checkstring(state, 2);
  luaL_checktype(state, 3, LUA_TTABLE);

  const auto thread_local_cluster = filter.clusterManager().getThreadLocalCluster(cluster);
  if (thread_local_cluster == nullptr) {
    luaL_error(state, "http call cluster invalid. Must be configured");
    return nullptr;
  }

  auto headers = Http::RequestHeaderMapImpl::create();
  buildHeadersFromTable(*headers, state, 3);
  Http::RequestMessagePtr message(new Http::RequestMessageImpl(std::move(headers)));

  // Check that we were provided certain headers.
  if (message->headers().Path() == nullptr || message->headers().Method() == nullptr ||
      message->headers().Host() == nullptr) {
    luaL_error(state, "http call headers must include ':path', ':method', and ':authority'");
    return nullptr;
  }

  size_t body_size;
  const char* body = luaL_optlstring(state, 4, nullptr, &body_size);
  if (body != nullptr) {
    message->body().add(body, body_size);
    message->headers().setContentLength(body_size);
  }

  return thread_local_cluster->httpAsyncClient().send(std::move(message), callbacks, options);
}

} // namespace

PerLuaCodeSetup::PerLuaCodeSetup(const std::string& lua_code, ThreadLocal::SlotAllocator& tls)
    : lua_state_(lua_code, tls) {
  lua_state_.registerType<Filters::Common::Lua::BufferWrapper>();
  lua_state_.registerType<Filters::Common::Lua::MetadataMapWrapper>();
  lua_state_.registerType<Filters::Common::Lua::MetadataMapIterator>();
  lua_state_.registerType<Filters::Common::Lua::ConnectionWrapper>();
  lua_state_.registerType<Filters::Common::Lua::SslConnectionWrapper>();
  lua_state_.registerType<Filters::Common::Lua::ParsedX509NameWrapper>();
  lua_state_.registerType<HeaderMapWrapper>();
  lua_state_.registerType<HeaderMapIterator>();
  lua_state_.registerType<StreamInfoWrapper>();
  lua_state_.registerType<DynamicMetadataMapWrapper>();
  lua_state_.registerType<DynamicMetadataMapIterator>();
  lua_state_.registerType<FilterStateWrapper>();
  lua_state_.registerType<StreamHandleWrapper>();
  lua_state_.registerType<PublicKeyWrapper>();
  lua_state_.registerType<ConnectionStreamInfoWrapper>();
  lua_state_.registerType<ConnectionDynamicMetadataMapWrapper>();
  lua_state_.registerType<ConnectionDynamicMetadataMapIterator>();
  lua_state_.registerType<VirtualHostWrapper>();
  lua_state_.registerType<RouteWrapper>();

  const Filters::Common::Lua::InitializerList initializers(
      // EnvoyTimestampResolution "enum".
      {
          [](lua_State* state) {
            lua_newtable(state);
            { LUA_ENUM(state, MILLISECOND, Timestamp::Resolution::Millisecond); }
            { LUA_ENUM(state, MICROSECOND, Timestamp::Resolution::Microsecond); }
            lua_setglobal(state, "EnvoyTimestampResolution");
          },
          // Add more initializers here.
      });

  request_function_slot_ = lua_state_.registerGlobal("envoy_on_request", initializers);
  if (lua_state_.getGlobalRef(request_function_slot_) == LUA_REFNIL) {
    ENVOY_LOG(info, "envoy_on_request() function not found. Lua filter will not hook requests.");
  }

  response_function_slot_ = lua_state_.registerGlobal("envoy_on_response", initializers);
  if (lua_state_.getGlobalRef(response_function_slot_) == LUA_REFNIL) {
    ENVOY_LOG(info, "envoy_on_response() function not found. Lua filter will not hook responses.");
  }
}

StreamHandleWrapper::StreamHandleWrapper(Filters::Common::Lua::Coroutine& coroutine,
                                         Http::RequestOrResponseHeaderMap& headers, bool end_stream,
                                         Filter& filter, FilterCallbacks& callbacks,
                                         TimeSource& time_source)
    : coroutine_(coroutine), headers_(headers), end_stream_(end_stream), filter_(filter),
      callbacks_(callbacks), yield_callback_([this]() {
        if (state_ == State::Running) {
          throw Filters::Common::Lua::LuaException("script performed an unexpected yield");
        }
      }),
      time_source_(time_source) {}

Http::FilterHeadersStatus StreamHandleWrapper::start(int function_ref) {
  // We are on the top of the stack.
  coroutine_.start(function_ref, 1, yield_callback_);
  Http::FilterHeadersStatus status =
      (state_ == State::WaitForBody || state_ == State::HttpCall || state_ == State::Responded)
          ? Http::FilterHeadersStatus::StopIteration
          : Http::FilterHeadersStatus::Continue;

  if (status == Http::FilterHeadersStatus::Continue) {
    headers_continued_ = true;
  }

  return status;
}

Http::FilterDataStatus StreamHandleWrapper::onData(Buffer::Instance& data, bool end_stream) {
  ASSERT(!end_stream_);
  end_stream_ = end_stream;
  saw_body_ = true;

  if (state_ == State::WaitForBodyChunk) {
    ENVOY_LOG(trace, "resuming for next body chunk");
    Filters::Common::Lua::LuaDeathRef<Filters::Common::Lua::BufferWrapper> wrapper(
        Filters::Common::Lua::BufferWrapper::create(coroutine_.luaState(), headers_, data), true);
    state_ = State::Running;
    resumeCoroutine(1, yield_callback_);
  } else if (state_ == State::WaitForBody && end_stream_) {
    ENVOY_LOG(debug, "resuming body due to end stream");
    callbacks_.addData(data);
    state_ = State::Running;
    resumeCoroutine(luaBody(coroutine_.luaState()), yield_callback_);
  } else if (state_ == State::WaitForTrailers && end_stream_) {
    ENVOY_LOG(debug, "resuming nil trailers due to end stream");
    state_ = State::Running;
    resumeCoroutine(0, yield_callback_);
  }

  if (state_ == State::HttpCall) {
    return Http::FilterDataStatus::StopIterationAndWatermark;
  } else if (state_ == State::WaitForBody) {
    ENVOY_LOG(trace, "buffering body");
    return Http::FilterDataStatus::StopIterationAndBuffer;
  } else if (state_ == State::Responded) {
    return Http::FilterDataStatus::StopIterationNoBuffer;
  } else {
    headers_continued_ = true;
    return Http::FilterDataStatus::Continue;
  }
}

Http::FilterTrailersStatus StreamHandleWrapper::onTrailers(Http::HeaderMap& trailers) {
  ASSERT(!end_stream_);
  end_stream_ = true;
  trailers_ = &trailers;

  if (state_ == State::WaitForBodyChunk) {
    ENVOY_LOG(debug, "resuming nil body chunk due to trailers");
    state_ = State::Running;
    resumeCoroutine(0, yield_callback_);
  } else if (state_ == State::WaitForBody) {
    ENVOY_LOG(debug, "resuming body due to trailers");
    state_ = State::Running;
    resumeCoroutine(luaBody(coroutine_.luaState()), yield_callback_);
  }

  if (state_ == State::WaitForTrailers) {
    // Mimic a call to trailers which will push the trailers onto the stack and then resume.
    state_ = State::Running;
    resumeCoroutine(luaTrailers(coroutine_.luaState()), yield_callback_);
  }

  Http::FilterTrailersStatus status = (state_ == State::HttpCall || state_ == State::Responded)
                                          ? Http::FilterTrailersStatus::StopIteration
                                          : Http::FilterTrailersStatus::Continue;

  if (status == Http::FilterTrailersStatus::Continue) {
    headers_continued_ = true;
  }

  return status;
}

int StreamHandleWrapper::luaRespond(lua_State* state) {
  ASSERT(state_ == State::Running);

  if (headers_continued_) {
    luaL_error(state, "respond() cannot be called if headers have been continued");
  }

  luaL_checktype(state, 2, LUA_TTABLE);
  size_t body_size;
  const char* raw_body = luaL_optlstring(state, 3, nullptr, &body_size);
  auto headers = Http::ResponseHeaderMapImpl::create();
  buildHeadersFromTable(*headers, state, 2);

  uint64_t status;
  if (!absl::SimpleAtoi(headers->getStatusValue(), &status) || status < 200 || status >= 600) {
    luaL_error(state, ":status must be between 200-599");
  }

  Buffer::InstancePtr body;
  if (raw_body != nullptr) {
    body = std::make_unique<Buffer::OwnedImpl>(raw_body, body_size);
    headers->setContentLength(body_size);
  }

  // Once we respond we treat that as the end of the script even if there is more code. Thus we
  // yield.
  callbacks_.respond(std::move(headers), body.get(), state);
  state_ = State::Responded;
  return lua_yield(state, 0);
}

int StreamHandleWrapper::luaHttpCall(lua_State* state) {
  ASSERT(state_ == State::Running);

  StreamHandleWrapper::HttpCallOptions options;
  options.request_options_
      .setParentSpan(callbacks_.activeSpan())
      // By default, do not enforce a sampling decision on this `httpCall`'s span.
      // Instead, reuse the parent span's sampling decision. Callers can override
      // this default with the `trace_sampled` flag in the table argument below.
      .setSampled(absl::nullopt);

  // Check if the last argument is table of options. For example:
  // handle:httpCall(cluster, headers, body, {["timeout"] = 200, ...}).
  if (const bool has_options = lua_istable(state, HttpCallOptionsIndex); has_options) {
    parseOptionsFromTable(state, HttpCallOptionsIndex, options);
  } else {
    if (!lua_isnone(state, AsyncFlagIndex) && !lua_isboolean(state, AsyncFlagIndex)) {
      luaL_error(state, "http call asynchronous flag must be 'true', 'false', or empty");
    }

    // When the last argument is not option table, the timeout is provided at the 5th index.
    int timeout_ms = luaL_checkint(state, 5);
    if (timeout_ms < 0) {
      luaL_error(state, "http call timeout must be >= 0");
    }
    options.request_options_.setTimeout(std::chrono::milliseconds(timeout_ms));
    options.is_async_request_ = lua_toboolean(state, AsyncFlagIndex);
  }

  return doHttpCall(state, options);
}

int StreamHandleWrapper::doHttpCall(lua_State* state, const HttpCallOptions& options) {
  if (options.is_async_request_) {
    makeHttpCall(state, filter_, options.request_options_, noopCallbacks());
    return 0;
  }

  http_request_ = makeHttpCall(state, filter_, options.request_options_, *this);
  if (http_request_ != nullptr) {
    state_ = State::HttpCall;
    return_duplicate_headers_ = options.return_duplicate_headers_;
    return lua_yield(state, 0);
  } else {
    // Immediate failure case. The return arguments are already on the stack.
    ASSERT(lua_gettop(state) >= 2);
    return 2;
  }
}

void StreamHandleWrapper::onSuccess(const Http::AsyncClient::Request&,
                                    Http::ResponseMessagePtr&& response) {
  ASSERT(state_ == State::HttpCall || state_ == State::Running);
  ENVOY_LOG(debug, "async HTTP response complete");
  http_request_ = nullptr;

  // We need to build a table with the headers as return param 1. The body will be return param 2.
  lua_newtable(coroutine_.luaState());

  if (return_duplicate_headers_) {
    absl::flat_hash_map<absl::string_view, absl::InlinedVector<absl::string_view, 1>> headers;
    headers.reserve(response->headers().size());

    response->headers().iterate([&headers](const Http::HeaderEntry& header) {
      headers[header.key().getStringView()].push_back(header.value().getStringView());
      return Http::HeaderMap::Iterate::Continue;
    });

    auto lua_state = coroutine_.luaState();
    for (const auto& header : headers) {
      lua_pushlstring(lua_state, header.first.data(), header.first.size());
      ASSERT(!header.second.empty());
      // If there are multiple values for same header name then create table for these values.
      if (header.second.size() > 1) {
        lua_newtable(lua_state);
        for (size_t i = 0; i < header.second.size(); i++) {
          lua_pushinteger(lua_state, i + 1);
          lua_pushlstring(lua_state, header.second[i].data(), header.second[i].size());
          lua_settable(lua_state, -3);
        }
      } else {
        lua_pushlstring(lua_state, header.second[0].data(), header.second[0].size());
      }

      lua_settable(lua_state, -3);
    }
  } else {
    response->headers().iterate([lua_State = coroutine_.luaState()](
                                    const Http::HeaderEntry& header) -> Http::HeaderMap::Iterate {
      lua_pushlstring(lua_State, header.key().getStringView().data(),
                      header.key().getStringView().size());
      lua_pushlstring(lua_State, header.value().getStringView().data(),
                      header.value().getStringView().size());
      lua_settable(lua_State, -3);
      return Http::HeaderMap::Iterate::Continue;
    });
  }

  // TODO(mattklein123): Avoid double copy here.
  if (response->body().length() > 0) {
    lua_pushlstring(coroutine_.luaState(), response->bodyAsString().data(),
                    response->body().length());
  } else {
    lua_pushnil(coroutine_.luaState());
  }

  // In the immediate failure case, we are just going to immediately return to the script. We
  // have already pushed the return arguments onto the stack.
  if (state_ == State::HttpCall) {
    state_ = State::Running;
    markLive();

    TRY_NEEDS_AUDIT {
      resumeCoroutine(2, yield_callback_);
      markDead();
    }
    END_TRY catch (const Filters::Common::Lua::LuaException& e) { filter_.scriptError(e); }

    if (state_ == State::Running) {
      headers_continued_ = true;
      callbacks_.continueIteration();
    }
  }
}

void StreamHandleWrapper::onFailure(const Http::AsyncClient::Request& request,
                                    Http::AsyncClient::FailureReason) {
  ASSERT(state_ == State::HttpCall || state_ == State::Running);
  ENVOY_LOG(debug, "async HTTP failure");

  // Just fake a basic 503 response.
  Http::ResponseMessagePtr response_message(
      new Http::ResponseMessageImpl(Http::createHeaderMap<Http::ResponseHeaderMapImpl>(
          {{Http::Headers::get().Status,
            std::to_string(enumToInt(Http::Code::ServiceUnavailable))}})));
  response_message->body().add("upstream failure");
  onSuccess(request, std::move(response_message));
}

int StreamHandleWrapper::luaHeaders(lua_State* state) {
  ASSERT(state_ == State::Running);

  if (headers_wrapper_.get() != nullptr) {
    headers_wrapper_.pushStack();
  } else {
    headers_wrapper_.reset(HeaderMapWrapper::create(state, headers_,
                                                    [this] {
                                                      // If we are about to do a modifiable header
                                                      // operation, blow away the route cache. We
                                                      // could be a little more intelligent about
                                                      // when we do this so the performance would be
                                                      // higher, but this is simple and will get the
                                                      // job done for now. This is a NOP on the
                                                      // encoder path.
                                                      if (!headers_continued_) {
                                                        callbacks_.onHeadersModified();
                                                      }

                                                      return !headers_continued_;
                                                    }),
                           true);
  }
  return 1;
}

int StreamHandleWrapper::luaBody(lua_State* state) {
  ASSERT(state_ == State::Running);

  bool always_wrap_body = false;

  if (lua_gettop(state) >= 2) {
    luaL_checktype(state, 2, LUA_TBOOLEAN);
    always_wrap_body = lua_toboolean(state, 2);
  }

  if (end_stream_) {
    if (!buffered_body_ && saw_body_) {
      return luaL_error(state, "cannot call body() after body has been streamed");
    } else {
      if (body_wrapper_.get() != nullptr) {
        body_wrapper_.pushStack();
      } else {
        if (callbacks_.bufferedBody() == nullptr) {
          ENVOY_LOG(debug, "end stream. no body");

          if (!always_wrap_body) {
            return 0;
          }

          Buffer::OwnedImpl body(EMPTY_STRING);
          callbacks_.addData(body);
        }

        body_wrapper_.reset(
            Filters::Common::Lua::BufferWrapper::create(
                state, headers_, const_cast<Buffer::Instance&>(*callbacks_.bufferedBody())),
            true);
      }
      return 1;
    }
  } else if (saw_body_) {
    return luaL_error(state, "cannot call body() after body streaming has started");
  } else {
    ENVOY_LOG(debug, "yielding for full body");
    state_ = State::WaitForBody;
    buffered_body_ = true;
    return lua_yield(state, 0);
  }
}

int StreamHandleWrapper::luaBodyChunks(lua_State* state) {
  ASSERT(state_ == State::Running);

  if (saw_body_) {
    luaL_error(state, "cannot call bodyChunks after body processing has begun");
  }

  // We are currently at the top of the stack. Push a closure that has us as the upvalue.
  lua_pushcclosure(state, static_luaBodyIterator, 1);
  return 1;
}

int StreamHandleWrapper::luaBodyIterator(lua_State* state) {
  ASSERT(state_ == State::Running);

  if (end_stream_) {
    ENVOY_LOG(debug, "body complete. no more body chunks");
    return 0;
  } else {
    ENVOY_LOG(debug, "yielding for next body chunk");
    state_ = State::WaitForBodyChunk;
    return lua_yield(state, 0);
  }
}

int StreamHandleWrapper::luaTrailers(lua_State* state) {
  ASSERT(state_ == State::Running);

  if (end_stream_ && trailers_ == nullptr) {
    ENVOY_LOG(debug, "end stream. no trailers");
    return 0;
  } else if (trailers_ != nullptr) {
    if (trailers_wrapper_.get() != nullptr) {
      trailers_wrapper_.pushStack();
    } else {
      trailers_wrapper_.reset(HeaderMapWrapper::create(state, *trailers_, []() { return true; }),
                              true);
    }
    return 1;
  } else {
    ENVOY_LOG(debug, "yielding for trailers");
    state_ = State::WaitForTrailers;
    return lua_yield(state, 0);
  }
}

int StreamHandleWrapper::luaMetadata(lua_State* state) {
  ASSERT(state_ == State::Running);
  if (metadata_wrapper_.get() != nullptr) {
    metadata_wrapper_.pushStack();
  } else {
    metadata_wrapper_.reset(
        Filters::Common::Lua::MetadataMapWrapper::create(state, callbacks_.metadata()), true);
  }
  return 1;
}

int StreamHandleWrapper::luaVirtualHost(lua_State* state) {
  ASSERT(state_ == State::Running);
  if (virtual_host_wrapper_.get() != nullptr) {
    virtual_host_wrapper_.pushStack();
  } else {
    virtual_host_wrapper_.reset(
        VirtualHostWrapper::create(state, callbacks_.streamInfo(), callbacks_.filterConfigName()),
        true);
  }
  return 1;
}

int StreamHandleWrapper::luaRoute(lua_State* state) {
  ASSERT(state_ == State::Running);
  if (route_wrapper_.get() != nullptr) {
    route_wrapper_.pushStack();
  } else {
    route_wrapper_.reset(
        RouteWrapper::create(state, callbacks_.streamInfo(), callbacks_.filterConfigName()), true);
  }
  return 1;
}

int StreamHandleWrapper::luaStreamInfo(lua_State* state) {
  ASSERT(state_ == State::Running);
  if (stream_info_wrapper_.get() != nullptr) {
    stream_info_wrapper_.pushStack();
  } else {
    stream_info_wrapper_.reset(StreamInfoWrapper::create(state, callbacks_.streamInfo()), true);
  }
  return 1;
}

int StreamHandleWrapper::luaConnectionStreamInfo(lua_State* state) {
  ASSERT(state_ == State::Running);
  if (connection_stream_info_wrapper_.get() != nullptr) {
    connection_stream_info_wrapper_.pushStack();
  } else {
    connection_stream_info_wrapper_.reset(
        ConnectionStreamInfoWrapper::create(state, callbacks_.connection()->streamInfo()), true);
  }
  return 1;
}

int StreamHandleWrapper::luaConnection(lua_State* state) {
  ASSERT(state_ == State::Running);
  if (connection_wrapper_.get() != nullptr) {
    connection_wrapper_.pushStack();
  } else {
    connection_wrapper_.reset(
        Filters::Common::Lua::ConnectionWrapper::create(state, callbacks_.connection()), true);
  }
  return 1;
}

int StreamHandleWrapper::luaVerifySignature(lua_State* state) {
  // Step 1: Get hash function.
  absl::string_view hash = luaL_checkstring(state, 2);

  // Step 2: Get the key pointer.
  auto key = luaL_checkstring(state, 3);
  auto ptr = public_key_storage_.find(key);
  if (ptr == public_key_storage_.end()) {
    luaL_error(state, "invalid public key");
    return 0;
  }

  // Step 3: Get signature from args.
  const char* signature = luaL_checkstring(state, 4);
  int sig_len = luaL_checknumber(state, 5);
  const std::vector<uint8_t> sig_vec(signature, signature + sig_len);

  // Step 4: Get clear text from args.
  const char* clear_text = luaL_checkstring(state, 6);
  int text_len = luaL_checknumber(state, 7);
  const std::vector<uint8_t> text_vec(clear_text, clear_text + text_len);

  // Step 5: Verify signature.
  auto& crypto_util = Envoy::Common::Crypto::UtilitySingleton::get();
  auto output = crypto_util.verifySignature(hash, *ptr->second, sig_vec, text_vec);
  if (output.ok()) {
    lua_pushboolean(state, true);
    lua_pushnil(state);
  } else {
    lua_pushboolean(state, false);
    lua_pushlstring(state, output.message().data(), output.message().size());
  }
  return 2;
}

int StreamHandleWrapper::luaImportPublicKey(lua_State* state) {
  // Get byte array and the length.
  const char* str = luaL_checkstring(state, 2);
  int n = luaL_checknumber(state, 3);
  std::vector<uint8_t> key(str, str + n);
  if (public_key_wrapper_.get() != nullptr) {
    public_key_wrapper_.pushStack();
  } else {
    auto& crypto_util = Envoy::Common::Crypto::UtilitySingleton::get();
    Envoy::Common::Crypto::PKeyObjectPtr crypto_ptr = crypto_util.importPublicKeyDER(key);
    if (crypto_ptr == nullptr) {
      // Failed to import key, create empty wrapper
      public_key_wrapper_.reset(PublicKeyWrapper::create(state, EMPTY_STRING), true);
      return 1;
    }
    EVP_PKEY* pkey = crypto_ptr->getEVP_PKEY();
    if (pkey == nullptr) {
      // TODO(dio): Call luaL_error here instead of failing silently. However, the current behavior
      // is to return nil (when calling get() to the wrapped object, hence we create a wrapper
      // initialized by an empty string here) when importing a public key is failed.
      public_key_wrapper_.reset(PublicKeyWrapper::create(state, EMPTY_STRING), true);
      return 1;
    }

    public_key_storage_.insert({std::string(str).substr(0, n), std::move(crypto_ptr)});
    public_key_wrapper_.reset(PublicKeyWrapper::create(state, str), true);
  }

  return 1;
}

int StreamHandleWrapper::luaBase64Escape(lua_State* state) {
  absl::string_view input = Filters::Common::Lua::getStringViewFromLuaString(state, 2);
  auto output = absl::Base64Escape(input);
  lua_pushlstring(state, output.data(), output.size());

  return 1;
}

int StreamHandleWrapper::luaTimestamp(lua_State* state) {
  auto now = time_source_.systemTime().time_since_epoch();

  absl::string_view unit_parameter = luaL_optstring(state, 2, "");

  auto milliseconds_since_epoch =
      std::chrono::duration_cast<std::chrono::milliseconds>(now).count();

  absl::uint128 resolution_as_int_from_state = 0;
  if (unit_parameter.empty()) {
    lua_pushnumber(state, milliseconds_since_epoch);
  } else if (absl::SimpleAtoi(unit_parameter, &resolution_as_int_from_state) &&
             resolution_as_int_from_state == enumToInt(Timestamp::Resolution::Millisecond)) {
    lua_pushnumber(state, milliseconds_since_epoch);
  } else {
    luaL_error(state, "timestamp format must be MILLISECOND.");
  }

  return 1;
}

int StreamHandleWrapper::luaTimestampString(lua_State* state) {
  auto now = time_source_.systemTime().time_since_epoch();

  absl::string_view unit_parameter = luaL_optstring(state, 2, "");
  auto resolution = getTimestampResolution(unit_parameter);
  if (resolution == Timestamp::Resolution::Millisecond) {
    auto milliseconds_since_epoch =
        std::chrono::duration_cast<std::chrono::milliseconds>(now).count();
    std::string timestamp = std::to_string(milliseconds_since_epoch);
    lua_pushlstring(state, timestamp.data(), timestamp.size());
  } else if (resolution == Timestamp::Resolution::Microsecond) {
    auto microseconds_since_epoch =
        std::chrono::duration_cast<std::chrono::microseconds>(now).count();
    std::string timestamp = std::to_string(microseconds_since_epoch);
    lua_pushlstring(state, timestamp.data(), timestamp.size());
  } else {
    luaL_error(state, "timestamp format must be MILLISECOND or MICROSECOND.");
  }
  return 1;
}

enum Timestamp::Resolution
StreamHandleWrapper::getTimestampResolution(absl::string_view unit_parameter) {
  auto resolution = Timestamp::Resolution::Undefined;

  absl::uint128 resolution_as_int_from_state = 0;
  if (unit_parameter.empty()) {
    resolution = Timestamp::Resolution::Millisecond;
  } else if (absl::SimpleAtoi(unit_parameter, &resolution_as_int_from_state) &&
             resolution_as_int_from_state == enumToInt(Timestamp::Resolution::Millisecond)) {
    resolution = Timestamp::Resolution::Millisecond;
  } else if (absl::SimpleAtoi(unit_parameter, &resolution_as_int_from_state) &&
             resolution_as_int_from_state == enumToInt(Timestamp::Resolution::Microsecond)) {
    resolution = Timestamp::Resolution::Microsecond;
  }
  return resolution;
}

FilterConfig::FilterConfig(const envoy::extensions::filters::http::lua::v3::Lua& proto_config,
                           ThreadLocal::SlotAllocator& tls,
                           Upstream::ClusterManager& cluster_manager, Api::Api& api,
                           Stats::Scope& scope, const std::string& stats_prefix)
    : cluster_manager_(cluster_manager),
      clear_route_cache_(
          proto_config.has_clear_route_cache() ? proto_config.clear_route_cache().value() : true),
      stats_(generateStats(stats_prefix, proto_config.stat_prefix(), scope)) {
  if (proto_config.has_default_source_code()) {
    if (!proto_config.inline_code().empty()) {
      throw EnvoyException("Error: Only one of `inline_code` or `default_source_code` can be set "
                           "for the Lua filter.");
    }

    const std::string code = THROW_OR_RETURN_VALUE(
        Config::DataSource::read(proto_config.default_source_code(), true, api), std::string);
    default_lua_code_setup_ = std::make_unique<PerLuaCodeSetup>(code, tls);
  } else if (!proto_config.inline_code().empty()) {
    default_lua_code_setup_ = std::make_unique<PerLuaCodeSetup>(proto_config.inline_code(), tls);
  }

  for (const auto& source : proto_config.source_codes()) {
    const std::string code =
        THROW_OR_RETURN_VALUE(Config::DataSource::read(source.second, true, api), std::string);
    auto per_lua_code_setup_ptr = std::make_unique<PerLuaCodeSetup>(code, tls);
    if (!per_lua_code_setup_ptr) {
      continue;
    }
    per_lua_code_setups_map_[source.first] = std::move(per_lua_code_setup_ptr);
  }
}

FilterConfigPerRoute::FilterConfigPerRoute(
    const envoy::extensions::filters::http::lua::v3::LuaPerRoute& config,
    Server::Configuration::ServerFactoryContext& context)
    : disabled_(config.disabled()), name_(config.name()), filter_context_(config.filter_context()) {
  if (disabled_ || !name_.empty()) {
    return; // Filter is disabled or explicit script name is provided.
  }
  if (config.has_source_code()) {
    // Read and parse the inline Lua code defined in the route configuration.
    const std::string code_str = THROW_OR_RETURN_VALUE(
        Config::DataSource::read(config.source_code(), true, context.api()), std::string);
    per_lua_code_setup_ptr_ = std::make_unique<PerLuaCodeSetup>(code_str, context.threadLocal());
  }
}

void Filter::onDestroy() {
  destroyed_ = true;
  if (request_stream_wrapper_.get()) {
    request_stream_wrapper_.get()->onReset();
  }
  if (response_stream_wrapper_.get()) {
    response_stream_wrapper_.get()->onReset();
  }
}

Http::FilterHeadersStatus
Filter::doHeaders(StreamHandleRef& handle, Filters::Common::Lua::CoroutinePtr& coroutine,
                  FilterCallbacks& callbacks, int function_ref, PerLuaCodeSetup* setup,
                  Http::RequestOrResponseHeaderMap& headers, bool end_stream) {
  if (function_ref == LUA_REFNIL) {
    return Http::FilterHeadersStatus::Continue;
  }
  ASSERT(setup);
  coroutine = setup->createCoroutine();

  handle.reset(StreamHandleWrapper::create(coroutine->luaState(), *coroutine, headers, end_stream,
                                           *this, callbacks, time_source_),
               true);

  Http::FilterHeadersStatus status = Http::FilterHeadersStatus::Continue;
  TRY_NEEDS_AUDIT {
    // The counter will increment twice if the supplied script has both request and response
    // handles. This is intentionally kept so as to provide consistency with the way the 'errors'
    // counter is incremented.
    stats_.executions_.inc();
    status = handle.get()->start(function_ref);
    handle.markDead();
  }
  END_TRY catch (const Filters::Common::Lua::LuaException& e) { scriptError(e); }

  return status;
}

Http::FilterDataStatus Filter::doData(StreamHandleRef& handle, Buffer::Instance& data,
                                      bool end_stream) {
  Http::FilterDataStatus status = Http::FilterDataStatus::Continue;
  if (handle.get() != nullptr) {
    TRY_NEEDS_AUDIT {
      handle.markLive();
      status = handle.get()->onData(data, end_stream);
      handle.markDead();
    }
    END_TRY catch (const Filters::Common::Lua::LuaException& e) { scriptError(e); }
  }

  return status;
}

Http::FilterTrailersStatus Filter::doTrailers(StreamHandleRef& handle, Http::HeaderMap& trailers) {
  Http::FilterTrailersStatus status = Http::FilterTrailersStatus::Continue;
  if (handle.get() != nullptr) {
    TRY_NEEDS_AUDIT {
      handle.markLive();
      status = handle.get()->onTrailers(trailers);
      handle.markDead();
    }
    END_TRY catch (const Filters::Common::Lua::LuaException& e) { scriptError(e); }
  }

  return status;
}

void Filter::scriptError(const Filters::Common::Lua::LuaException& e) {
  stats_.errors_.inc();
  scriptLog(spdlog::level::err, e.what());
  request_stream_wrapper_.reset();
  response_stream_wrapper_.reset();
}

int StreamHandleWrapper::luaSetUpstreamOverrideHost(lua_State* state) {
  // Get the host address argument
  size_t len;
  const char* host = luaL_checklstring(state, 2, &len);

  // Validate that host is not null and is an IP address
  if (host == nullptr) {
    luaL_error(state, "host argument is required");
  }
  if (!Http::Utility::parseAuthority(host).is_ip_address_) {
    luaL_error(state, "host is not a valid IP address");
  }

  // Get the optional strict flag (defaults to false)
  bool strict = false;
  if (lua_gettop(state) >= 3) {
    luaL_checktype(state, 3, LUA_TBOOLEAN);
    strict = lua_toboolean(state, 3);
  }

  // Set the upstream override host
  callbacks_.setUpstreamOverrideHost(std::make_pair(std::string(host, len), strict));

  return 0;
}

int StreamHandleWrapper::luaClearRouteCache(lua_State*) {
  callbacks_.clearRouteCache();
  return 0;
}

int StreamHandleWrapper::luaFilterContext(lua_State* state) {
  ASSERT(state_ == State::Running);
  if (filter_context_wrapper_.get() != nullptr) {
    filter_context_wrapper_.pushStack();
  } else {
    filter_context_wrapper_.reset(
        Filters::Common::Lua::MetadataMapWrapper::create(state, callbacks_.filterContext()), true);
  }
  return 1;
}

void Filter::DecoderCallbacks::respond(Http::ResponseHeaderMapPtr&& headers, Buffer::Instance* body,
                                       lua_State*) {
  uint64_t status = Http::Utility::getResponseStatus(*headers);
  auto modify_headers = [&headers](Http::ResponseHeaderMap& response_headers) {
    headers->iterate(
        [&response_headers](const Http::HeaderEntry& header) -> Http::HeaderMap::Iterate {
          response_headers.addCopy(Http::LowerCaseString(header.key().getStringView()),
                                   header.value().getStringView());
          return Http::HeaderMap::Iterate::Continue;
        });
  };
  callbacks_->sendLocalReply(static_cast<Envoy::Http::Code>(status), body ? body->toString() : "",
                             modify_headers, absl::nullopt,
                             HttpResponseCodeDetails::get().LuaResponse);
}

const Protobuf::Struct& Filter::DecoderCallbacks::metadata() const {
  return getMetadata(callbacks_);
}

void Filter::EncoderCallbacks::respond(Http::ResponseHeaderMapPtr&&, Buffer::Instance*,
                                       lua_State* state) {
  // TODO(mattklein123): Support response in response path if nothing has been continued
  // yet.
  luaL_error(state, "respond not currently supported in the response path");
}

const Protobuf::Struct& Filter::EncoderCallbacks::metadata() const {
  return getMetadata(callbacks_);
}

} // namespace Lua
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
