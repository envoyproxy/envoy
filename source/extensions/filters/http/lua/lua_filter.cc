#include "source/extensions/filters/http/lua/lua_filter.h"

#include <atomic>
#include <memory>

#include "envoy/http/codes.h"

#include "source/common/buffer/buffer_impl.h"
#include "source/common/common/assert.h"
#include "source/common/common/enum_to_int.h"
#include "source/common/config/datasource.h"
#include "source/common/crypto/utility.h"
#include "source/common/http/message_impl.h"

#include "absl/strings/escaping.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace Lua {

namespace {

struct HttpResponseCodeDetailValues {
  const absl::string_view LuaResponse = "lua_response";
};
using HttpResponseCodeDetails = ConstSingleton<HttpResponseCodeDetailValues>;

const std::string DEPRECATED_LUA_NAME = "envoy.lua";

std::atomic<bool>& deprecatedNameLogged() {
  MUTABLE_CONSTRUCT_ON_FIRST_USE(std::atomic<bool>, false);
}

// Checks if deprecated metadata names are allowed. On the first check only it will log either
// a warning (indicating the name should be updated) or an error (the feature is off and the
// name is not allowed). When warning, the deprecated feature stat is incremented. Subsequent
// checks do not log since this check is done in potentially high-volume request paths.
bool allowDeprecatedMetadataName() {
  if (!deprecatedNameLogged().exchange(true)) {
    // Have not logged yet, so use the logging test.
    return Extensions::Common::Utility::ExtensionNameUtil::allowDeprecatedExtensionName(
        "http filter", DEPRECATED_LUA_NAME, "envoy.filters.http.lua");
  }

  // We have logged (or another thread will do so momentarily), so just check whether the
  // deprecated name is allowed.
  auto status = Extensions::Common::Utility::ExtensionNameUtil::deprecatedExtensionNameStatus();
  return status == Extensions::Common::Utility::ExtensionNameUtil::Status::Warn;
}

const ProtobufWkt::Struct& getMetadata(Http::StreamFilterCallbacks* callbacks) {
  if (callbacks->route() == nullptr) {
    return ProtobufWkt::Struct::default_instance();
  }
  const auto& metadata = callbacks->route()->metadata();

  {
    const auto& filter_it = metadata.filter_metadata().find("envoy.filters.http.lua");
    if (filter_it != metadata.filter_metadata().end()) {
      return filter_it->second;
    }
  }

  // TODO(zuercher): Remove this block when deprecated filter names are removed.
  {
    const auto& filter_it = metadata.filter_metadata().find(DEPRECATED_LUA_NAME);
    if (filter_it != metadata.filter_metadata().end()) {
      // Use the non-throwing check here because this happens at request time.
      if (allowDeprecatedMetadataName()) {
        return filter_it->second;
      }
    }
  }

  return ProtobufWkt::Struct::default_instance();
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
                                         Tracing::Span& parent_span,
                                         Http::AsyncClient::Callbacks& callbacks) {
  const std::string cluster = luaL_checkstring(state, 2);
  luaL_checktype(state, 3, LUA_TTABLE);
  size_t body_size;
  const char* body = luaL_optlstring(state, 4, nullptr, &body_size);
  int timeout_ms = luaL_checkint(state, 5);
  if (timeout_ms < 0) {
    luaL_error(state, "http call timeout must be >= 0");
  }

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
  }

  if (body != nullptr) {
    message->body().add(body, body_size);
    message->headers().setContentLength(body_size);
  }

  absl::optional<std::chrono::milliseconds> timeout;
  if (timeout_ms > 0) {
    timeout = std::chrono::milliseconds(timeout_ms);
  }

  auto options = Http::AsyncClient::RequestOptions().setTimeout(timeout).setParentSpan(parent_span);
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
  lua_state_.registerType<HeaderMapWrapper>();
  lua_state_.registerType<HeaderMapIterator>();
  lua_state_.registerType<StreamInfoWrapper>();
  lua_state_.registerType<DynamicMetadataMapWrapper>();
  lua_state_.registerType<DynamicMetadataMapIterator>();
  lua_state_.registerType<StreamHandleWrapper>();
  lua_state_.registerType<PublicKeyWrapper>();

  const Filters::Common::Lua::InitializerList initializers(
      // EnvoyTimestampResolution "enum".
      {
          [](lua_State* state) {
            lua_newtable(state);
            { LUA_ENUM(state, MILLISECOND, Timestamp::Resolution::Millisecond); }
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
                                         Http::HeaderMap& headers, bool end_stream, Filter& filter,
                                         FilterCallbacks& callbacks, TimeSource& time_source)
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
        Filters::Common::Lua::BufferWrapper::create(coroutine_.luaState(), data), true);
    state_ = State::Running;
    coroutine_.resume(1, yield_callback_);
  } else if (state_ == State::WaitForBody && end_stream_) {
    ENVOY_LOG(debug, "resuming body due to end stream");
    callbacks_.addData(data);
    state_ = State::Running;
    coroutine_.resume(luaBody(coroutine_.luaState()), yield_callback_);
  } else if (state_ == State::WaitForTrailers && end_stream_) {
    ENVOY_LOG(debug, "resuming nil trailers due to end stream");
    state_ = State::Running;
    coroutine_.resume(0, yield_callback_);
  }

  if (state_ == State::HttpCall || state_ == State::WaitForBody) {
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
    coroutine_.resume(0, yield_callback_);
  } else if (state_ == State::WaitForBody) {
    ENVOY_LOG(debug, "resuming body due to trailers");
    state_ = State::Running;
    coroutine_.resume(luaBody(coroutine_.luaState()), yield_callback_);
  }

  if (state_ == State::WaitForTrailers) {
    // Mimic a call to trailers which will push the trailers onto the stack and then resume.
    state_ = State::Running;
    coroutine_.resume(luaTrailers(coroutine_.luaState()), yield_callback_);
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

  const int async_flag_index = 6;
  if (!lua_isnone(state, async_flag_index) && !lua_isboolean(state, async_flag_index)) {
    luaL_error(state, "http call asynchronous flag must be 'true', 'false', or empty");
  }

  if (lua_toboolean(state, async_flag_index)) {
    return doAsynchronousHttpCall(state, callbacks_.activeSpan());
  } else {
    return doSynchronousHttpCall(state, callbacks_.activeSpan());
  }
}

int StreamHandleWrapper::doSynchronousHttpCall(lua_State* state, Tracing::Span& span) {
  http_request_ = makeHttpCall(state, filter_, span, *this);
  if (http_request_ != nullptr) {
    state_ = State::HttpCall;
    return lua_yield(state, 0);
  } else {
    // Immediate failure case. The return arguments are already on the stack.
    ASSERT(lua_gettop(state) >= 2);
    return 2;
  }
}

int StreamHandleWrapper::doAsynchronousHttpCall(lua_State* state, Tracing::Span& span) {
  makeHttpCall(state, filter_, span, noopCallbacks());
  return 0;
}

void StreamHandleWrapper::onSuccess(const Http::AsyncClient::Request&,
                                    Http::ResponseMessagePtr&& response) {
  ASSERT(state_ == State::HttpCall || state_ == State::Running);
  ENVOY_LOG(debug, "async HTTP response complete");
  http_request_ = nullptr;

  // We need to build a table with the headers as return param 1. The body will be return param 2.
  lua_newtable(coroutine_.luaState());
  response->headers().iterate([lua_State = coroutine_.luaState()](
                                  const Http::HeaderEntry& header) -> Http::HeaderMap::Iterate {
    lua_pushlstring(lua_State, header.key().getStringView().data(),
                    header.key().getStringView().length());
    lua_pushlstring(lua_State, header.value().getStringView().data(),
                    header.value().getStringView().length());
    lua_settable(lua_State, -3);
    return Http::HeaderMap::Iterate::Continue;
  });

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

    try {
      coroutine_.resume(2, yield_callback_);
      markDead();
    } catch (const Filters::Common::Lua::LuaException& e) {
      filter_.scriptError(e);
    }

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

        body_wrapper_.reset(Filters::Common::Lua::BufferWrapper::create(
                                state, const_cast<Buffer::Instance&>(*callbacks_.bufferedBody())),
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

int StreamHandleWrapper::luaStreamInfo(lua_State* state) {
  ASSERT(state_ == State::Running);
  if (stream_info_wrapper_.get() != nullptr) {
    stream_info_wrapper_.pushStack();
  } else {
    stream_info_wrapper_.reset(StreamInfoWrapper::create(state, callbacks_.streamInfo()), true);
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

int StreamHandleWrapper::luaLogTrace(lua_State* state) {
  const char* message = luaL_checkstring(state, 2);
  filter_.scriptLog(spdlog::level::trace, message);
  return 0;
}

int StreamHandleWrapper::luaLogDebug(lua_State* state) {
  const char* message = luaL_checkstring(state, 2);
  filter_.scriptLog(spdlog::level::debug, message);
  return 0;
}

int StreamHandleWrapper::luaLogInfo(lua_State* state) {
  const char* message = luaL_checkstring(state, 2);
  filter_.scriptLog(spdlog::level::info, message);
  return 0;
}

int StreamHandleWrapper::luaLogWarn(lua_State* state) {
  const char* message = luaL_checkstring(state, 2);
  filter_.scriptLog(spdlog::level::warn, message);
  return 0;
}

int StreamHandleWrapper::luaLogErr(lua_State* state) {
  const char* message = luaL_checkstring(state, 2);
  filter_.scriptLog(spdlog::level::err, message);
  return 0;
}

int StreamHandleWrapper::luaLogCritical(lua_State* state) {
  const char* message = luaL_checkstring(state, 2);
  filter_.scriptLog(spdlog::level::critical, message);
  return 0;
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
  lua_pushboolean(state, output.result_);
  if (output.result_) {
    lua_pushnil(state);
  } else {
    lua_pushlstring(state, output.error_message_.data(), output.error_message_.length());
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
    Envoy::Common::Crypto::CryptoObjectPtr crypto_ptr = crypto_util.importPublicKey(key);
    auto wrapper = Envoy::Common::Crypto::Access::getTyped<Envoy::Common::Crypto::PublicKeyObject>(
        *crypto_ptr);
    EVP_PKEY* pkey = wrapper->getEVP_PKEY();
    if (pkey == nullptr) {
      // TODO(dio): Call luaL_error here instead of failing silently. However, the current behavior
      // is to return nil (when calling get() to the wrapped object, hence we create a wrapper
      // initialized by an empty string here) when importing a public key is failed.
      public_key_wrapper_.reset(PublicKeyWrapper::create(state, EMPTY_STRING), true);
    }

    public_key_storage_.insert({std::string(str).substr(0, n), std::move(crypto_ptr)});
    public_key_wrapper_.reset(PublicKeyWrapper::create(state, str), true);
  }

  return 1;
}

int StreamHandleWrapper::luaBase64Escape(lua_State* state) {
  size_t input_size;
  const char* input = luaL_checklstring(state, 2, &input_size);
  auto output = absl::Base64Escape(absl::string_view(input, input_size));
  lua_pushlstring(state, output.data(), output.length());

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

FilterConfig::FilterConfig(const envoy::extensions::filters::http::lua::v3::Lua& proto_config,
                           ThreadLocal::SlotAllocator& tls,
                           Upstream::ClusterManager& cluster_manager, Api::Api& api)
    : cluster_manager_(cluster_manager) {
  auto global_setup_ptr = std::make_unique<PerLuaCodeSetup>(proto_config.inline_code(), tls);
  if (global_setup_ptr) {
    per_lua_code_setups_map_[GLOBAL_SCRIPT_NAME] = std::move(global_setup_ptr);
  }

  for (const auto& source : proto_config.source_codes()) {
    const std::string code = Config::DataSource::read(source.second, true, api);
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
    : main_thread_dispatcher_(context.dispatcher()), disabled_(config.disabled()),
      name_(config.name()) {
  if (disabled_ || !name_.empty()) {
    return;
  }
  // Read and parse the inline Lua code defined in the route configuration.
  const std::string code_str = Config::DataSource::read(config.source_code(), true, context.api());
  per_lua_code_setup_ptr_ = std::make_unique<PerLuaCodeSetup>(code_str, context.threadLocal());
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

Http::FilterHeadersStatus Filter::doHeaders(StreamHandleRef& handle,
                                            Filters::Common::Lua::CoroutinePtr& coroutine,
                                            FilterCallbacks& callbacks, int function_ref,
                                            PerLuaCodeSetup* setup, Http::HeaderMap& headers,
                                            bool end_stream) {
  if (function_ref == LUA_REFNIL) {
    return Http::FilterHeadersStatus::Continue;
  }
  ASSERT(setup);
  coroutine = setup->createCoroutine();

  handle.reset(StreamHandleWrapper::create(coroutine->luaState(), *coroutine, headers, end_stream,
                                           *this, callbacks, time_source_),
               true);

  Http::FilterHeadersStatus status = Http::FilterHeadersStatus::Continue;
  try {
    status = handle.get()->start(function_ref);
    handle.markDead();
  } catch (const Filters::Common::Lua::LuaException& e) {
    scriptError(e);
  }

  return status;
}

Http::FilterDataStatus Filter::doData(StreamHandleRef& handle, Buffer::Instance& data,
                                      bool end_stream) {
  Http::FilterDataStatus status = Http::FilterDataStatus::Continue;
  if (handle.get() != nullptr) {
    try {
      handle.markLive();
      status = handle.get()->onData(data, end_stream);
      handle.markDead();
    } catch (const Filters::Common::Lua::LuaException& e) {
      scriptError(e);
    }
  }

  return status;
}

Http::FilterTrailersStatus Filter::doTrailers(StreamHandleRef& handle, Http::HeaderMap& trailers) {
  Http::FilterTrailersStatus status = Http::FilterTrailersStatus::Continue;
  if (handle.get() != nullptr) {
    try {
      handle.markLive();
      status = handle.get()->onTrailers(trailers);
      handle.markDead();
    } catch (const Filters::Common::Lua::LuaException& e) {
      scriptError(e);
    }
  }

  return status;
}

void Filter::scriptError(const Filters::Common::Lua::LuaException& e) {
  scriptLog(spdlog::level::err, e.what());
  request_stream_wrapper_.reset();
  response_stream_wrapper_.reset();
}

void Filter::scriptLog(spdlog::level::level_enum level, const char* message) {
  switch (level) {
  case spdlog::level::trace:
    ENVOY_LOG(trace, "script log: {}", message);
    return;
  case spdlog::level::debug:
    ENVOY_LOG(debug, "script log: {}", message);
    return;
  case spdlog::level::info:
    ENVOY_LOG(info, "script log: {}", message);
    return;
  case spdlog::level::warn:
    ENVOY_LOG(warn, "script log: {}", message);
    return;
  case spdlog::level::err:
    ENVOY_LOG(error, "script log: {}", message);
    return;
  case spdlog::level::critical:
    ENVOY_LOG(critical, "script log: {}", message);
    return;
  case spdlog::level::off:
    NOT_IMPLEMENTED_GCOVR_EXCL_LINE;
    return;
  case spdlog::level::n_levels:
    NOT_REACHED_GCOVR_EXCL_LINE;
  }
}

void Filter::DecoderCallbacks::respond(Http::ResponseHeaderMapPtr&& headers, Buffer::Instance* body,
                                       lua_State*) {
  callbacks_->encodeHeaders(std::move(headers), body == nullptr,
                            HttpResponseCodeDetails::get().LuaResponse);
  if (body && !parent_.destroyed_) {
    callbacks_->encodeData(*body, true);
  }
}

const ProtobufWkt::Struct& Filter::DecoderCallbacks::metadata() const {
  return getMetadata(callbacks_);
}

void Filter::EncoderCallbacks::respond(Http::ResponseHeaderMapPtr&&, Buffer::Instance*,
                                       lua_State* state) {
  // TODO(mattklein123): Support response in response path if nothing has been continued
  // yet.
  luaL_error(state, "respond not currently supported in the response path");
}

const ProtobufWkt::Struct& Filter::EncoderCallbacks::metadata() const {
  return getMetadata(callbacks_);
}

} // namespace Lua
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
