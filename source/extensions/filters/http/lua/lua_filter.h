#pragma once

#include "envoy/extensions/filters/http/lua/v3/lua.pb.h"
#include "envoy/http/filter.h"
#include "envoy/stats/stats_macros.h"
#include "envoy/upstream/cluster_manager.h"

#include "source/common/crypto/utility.h"
#include "source/common/http/utility.h"
#include "source/extensions/filters/common/lua/wrappers.h"
#include "source/extensions/filters/http/common/factory_base.h"
#include "source/extensions/filters/http/lua/wrappers.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace Lua {

/**
 * All lua stats. @see stats_macros.h
 */
#define ALL_LUA_FILTER_STATS(COUNTER) COUNTER(errors)

/**
 * Struct definition for all Lua stats. @see stats_macros.h
 */
struct LuaFilterStats {
  ALL_LUA_FILTER_STATS(GENERATE_COUNTER_STRUCT)
};

class PerLuaCodeSetup : Logger::Loggable<Logger::Id::lua> {
public:
  PerLuaCodeSetup(const std::string& lua_code, ThreadLocal::SlotAllocator& tls);

  Extensions::Filters::Common::Lua::CoroutinePtr createCoroutine() {
    return lua_state_.createCoroutine();
  }

  int requestFunctionRef() { return lua_state_.getGlobalRef(request_function_slot_); }
  int responseFunctionRef() { return lua_state_.getGlobalRef(response_function_slot_); }

  uint64_t runtimeBytesUsed() { return lua_state_.runtimeBytesUsed(); }
  void runtimeGC() { return lua_state_.runtimeGC(); }

private:
  uint64_t request_function_slot_{};
  uint64_t response_function_slot_{};

  Filters::Common::Lua::ThreadLocalState lua_state_;
};

using PerLuaCodeSetupPtr = std::unique_ptr<PerLuaCodeSetup>;

/**
 * Callbacks used by a stream handler to access the filter.
 */
class FilterCallbacks {
public:
  virtual ~FilterCallbacks() = default;

  /**
   * Add data to the connection manager buffer.
   * @param data supplies the data to add.
   */
  virtual void addData(Buffer::Instance& data) PURE;

  /**
   * @return const Buffer::Instance* the currently buffered body.
   */
  virtual const Buffer::Instance* bufferedBody() PURE;

  /**
   * Continue filter iteration if iteration has been paused due to an async call.
   */
  virtual void continueIteration() PURE;

  /**
   * Called when headers have been modified by a script. This can only happen prior to headers
   * being continued.
   */
  virtual void onHeadersModified() PURE;

  /**
   * Perform an immediate response.
   * @param headers supplies the response headers.
   * @param body supplies the optional response body.
   * @param state supplies the active Lua state.
   */
  virtual void respond(Http::ResponseHeaderMapPtr&& headers, Buffer::Instance* body,
                       lua_State* state) PURE;

  /**
   * @return const ProtobufWkt::Struct& the value of metadata inside the lua filter scope of current
   * route entry.
   */
  virtual const ProtobufWkt::Struct& metadata() const PURE;

  /**
   * @return StreamInfo::StreamInfo& the current stream info handle. This handle is mutable to
   * accommodate write API e.g. setDynamicMetadata().
   */
  virtual StreamInfo::StreamInfo& streamInfo() PURE;

  /**
   * @return const Network::Connection* the current network connection handle.
   */
  virtual const Network::Connection* connection() const PURE;

  /**
   * @return const Tracing::Span& the current tracing active span.
   */
  virtual Tracing::Span& activeSpan() PURE;
};

class Filter;

/**
 * A wrapper for a currently running request/response. This is the primary handle passed to Lua.
 * The script interacts with Envoy entirely through this handle.
 */
class StreamHandleWrapper : public Filters::Common::Lua::BaseLuaObject<StreamHandleWrapper>,
                            public Http::AsyncClient::Callbacks {
public:
  /**
   * The state machine for a stream handler. In the current implementation everything the filter
   * does is a discrete state. This may become sub-optimal as we add other things that might
   * cause the filter to block.
   * TODO(mattklein123): Consider whether we should split the state machine into an overall state
   * and a blocking reason type.
   */
  enum class State {
    // Lua code is currently running or the script has finished.
    Running,
    // Lua script is blocked waiting for the next body chunk.
    WaitForBodyChunk,
    // Lua script is blocked waiting for the full body.
    WaitForBody,
    // Lua script is blocked waiting for trailers.
    WaitForTrailers,
    // Lua script is blocked waiting for the result of an HTTP call.
    HttpCall,
    // Lua script has done a direct response.
    Responded
  };

  struct HttpCallOptions {
    Http::AsyncClient::RequestOptions request_options_;
    bool is_async_request_{false};
    bool return_duplicate_headers_{false};
  };

  StreamHandleWrapper(Filters::Common::Lua::Coroutine& coroutine,
                      Http::RequestOrResponseHeaderMap& headers, bool end_stream, Filter& filter,
                      FilterCallbacks& callbacks, TimeSource& time_source);

  Http::FilterHeadersStatus start(int function_ref);
  Http::FilterDataStatus onData(Buffer::Instance& data, bool end_stream);
  Http::FilterTrailersStatus onTrailers(Http::HeaderMap& trailers);

  void onReset() {
    if (http_request_) {
      http_request_->cancel();
      http_request_ = nullptr;
    }
  }

  static ExportedFunctions exportedFunctions() {
    return {{"headers", static_luaHeaders},
            {"body", static_luaBody},
            {"bodyChunks", static_luaBodyChunks},
            {"trailers", static_luaTrailers},
            {"metadata", static_luaMetadata},
            {"logTrace", static_luaLogTrace},
            {"logDebug", static_luaLogDebug},
            {"logInfo", static_luaLogInfo},
            {"logWarn", static_luaLogWarn},
            {"logErr", static_luaLogErr},
            {"logCritical", static_luaLogCritical},
            {"httpCall", static_luaHttpCall},
            {"respond", static_luaRespond},
            {"streamInfo", static_luaStreamInfo},
            {"connection", static_luaConnection},
            {"importPublicKey", static_luaImportPublicKey},
            {"verifySignature", static_luaVerifySignature},
            {"base64Escape", static_luaBase64Escape},
            {"timestamp", static_luaTimestamp},
            {"timestampString", static_luaTimestampString}};
  }

private:
  /**
   * Perform an HTTP call to an upstream host.
   * @param 1 (string): The name of the upstream cluster to call. This cluster must be configured.
   * @param 2 (table): A table of HTTP headers. :method, :path, and :authority must be defined.
   * @param 3 (string): Body. Can be nil.
   * @param 4 (int): Timeout in milliseconds for the call.
   * @param 5 (bool): Optional flag. If true, filter continues without waiting for HTTP response
   * from upstream service. False/synchronous by default.
   * @return headers (table), body (string/nil)
   */
  DECLARE_LUA_FUNCTION(StreamHandleWrapper, luaHttpCall);

  /**
   * Perform an inline response. This call is currently only valid on the request path. Further
   * filter iteration will stop. No further script code will run after this call.
   * @param 1 (table): A table of HTTP headers. :status must be defined.
   * @param 2 (string): Body. Can be nil.
   */
  DECLARE_LUA_FUNCTION(StreamHandleWrapper, luaRespond);

  /**
   * @return a handle to the headers.
   */
  DECLARE_LUA_FUNCTION(StreamHandleWrapper, luaHeaders);

  /**
   * @return a handle to the full body or nil if there is no body. This call will cause the script
   *         to yield until the entire body is received (or if there is no body will return nil
   *         right away).
   *         NOTE: This call causes Envoy to buffer the body. The max buffer size is configured
   *         based on the currently active flow control settings.
   */
  DECLARE_LUA_FUNCTION(StreamHandleWrapper, luaBody);

  /**
   * @return an iterator that allows the script to iterate through all body chunks as they are
   *         received. The iterator will yield between body chunks. Envoy *will not* buffer
   *         the body chunks in this case, but the script can look at them as they go by.
   */
  DECLARE_LUA_FUNCTION(StreamHandleWrapper, luaBodyChunks);

  /**
   * @return a handle to the trailers or nil if there are no trailers. This call will cause the
   *         script to yield if Envoy does not yet know if there are trailers or not.
   */
  DECLARE_LUA_FUNCTION(StreamHandleWrapper, luaTrailers);

  /**
   * @return a handle to the metadata.
   */
  DECLARE_LUA_FUNCTION(StreamHandleWrapper, luaMetadata);

  /**
   * @return a handle to the stream info.
   */
  DECLARE_LUA_FUNCTION(StreamHandleWrapper, luaStreamInfo);

  /**
   * @return a handle to the network connection.
   */
  DECLARE_LUA_FUNCTION(StreamHandleWrapper, luaConnection);

  /**
   * Log a message to the Envoy log.
   * @param 1 (string): The log message.
   */
  DECLARE_LUA_FUNCTION(StreamHandleWrapper, luaLogTrace);
  DECLARE_LUA_FUNCTION(StreamHandleWrapper, luaLogDebug);
  DECLARE_LUA_FUNCTION(StreamHandleWrapper, luaLogInfo);
  DECLARE_LUA_FUNCTION(StreamHandleWrapper, luaLogWarn);
  DECLARE_LUA_FUNCTION(StreamHandleWrapper, luaLogErr);
  DECLARE_LUA_FUNCTION(StreamHandleWrapper, luaLogCritical);

  /**
   * Verify cryptographic signatures.
   * @param 1 (string) hash function(including SHA1, SHA224, SHA256, SHA384, SHA512)
   * @param 2 (void*)  pointer to public key
   * @param 3 (string) signature
   * @param 4 (int)    length of signature
   * @param 5 (string) clear text
   * @param 6 (int)    length of clear text
   * @return (bool, string) If the first element is true, the second element is empty; otherwise,
   * the second element stores the error message
   */
  DECLARE_LUA_FUNCTION(StreamHandleWrapper, luaVerifySignature);

  /**
   * Import public key.
   * @param 1 (string) keyder string
   * @param 2 (int)    length of keyder string
   * @return pointer to public key
   */
  DECLARE_LUA_FUNCTION(StreamHandleWrapper, luaImportPublicKey);

  /**
   * This is the closure/iterator returned by luaBodyChunks() above.
   */
  DECLARE_LUA_CLOSURE(StreamHandleWrapper, luaBodyIterator);

  /**
   * Base64 escape a string.
   * @param1 (string) string to be base64 escaped.
   * @return (string) base64 escaped string.
   */
  DECLARE_LUA_FUNCTION(StreamHandleWrapper, luaBase64Escape);

  /**
   * Timestamp.
   * @param1 (string) optional format (e.g. milliseconds_from_epoch, nanoseconds_from_epoch).
   * Defaults to milliseconds_from_epoch.
   * @return timestamp
   */
  DECLARE_LUA_FUNCTION(StreamHandleWrapper, luaTimestamp);

  /**
   * TimestampString.
   * @param1 (string) optional format (e.g. milliseconds_from_epoch, microseconds_from_epoch).
   * Defaults to milliseconds_from_epoch.
   * @return (string) timestamp.
   */
  DECLARE_LUA_FUNCTION(StreamHandleWrapper, luaTimestampString);

  enum Timestamp::Resolution getTimestampResolution(absl::string_view unit_parameter);

  int doHttpCall(lua_State* state, const HttpCallOptions& options);

  // Filters::Common::Lua::BaseLuaObject
  void onMarkDead() override {
    // Headers/body/trailers wrappers do not survive any yields. The user can request them
    // again across yields if needed.
    headers_wrapper_.reset();
    body_wrapper_.reset();
    trailers_wrapper_.reset();
    metadata_wrapper_.reset();
    stream_info_wrapper_.reset();
    connection_wrapper_.reset();
    public_key_wrapper_.reset();
  }

  // Http::AsyncClient::Callbacks
  void onSuccess(const Http::AsyncClient::Request&, Http::ResponseMessagePtr&&) override;
  void onFailure(const Http::AsyncClient::Request&, Http::AsyncClient::FailureReason) override;
  void onBeforeFinalizeUpstreamSpan(Tracing::Span&, const Http::ResponseHeaderMap*) override {}

  Filters::Common::Lua::Coroutine& coroutine_;
  Http::RequestOrResponseHeaderMap& headers_;
  bool end_stream_;
  bool headers_continued_{};
  bool buffered_body_{};
  bool saw_body_{};
  bool return_duplicate_headers_{};
  Filter& filter_;
  FilterCallbacks& callbacks_;
  Http::HeaderMap* trailers_{};
  Filters::Common::Lua::LuaDeathRef<HeaderMapWrapper> headers_wrapper_;
  Filters::Common::Lua::LuaDeathRef<Filters::Common::Lua::BufferWrapper> body_wrapper_;
  Filters::Common::Lua::LuaDeathRef<HeaderMapWrapper> trailers_wrapper_;
  Filters::Common::Lua::LuaDeathRef<Filters::Common::Lua::MetadataMapWrapper> metadata_wrapper_;
  Filters::Common::Lua::LuaDeathRef<StreamInfoWrapper> stream_info_wrapper_;
  Filters::Common::Lua::LuaDeathRef<Filters::Common::Lua::ConnectionWrapper> connection_wrapper_;
  Filters::Common::Lua::LuaDeathRef<PublicKeyWrapper> public_key_wrapper_;
  State state_{State::Running};
  std::function<void()> yield_callback_;
  Http::AsyncClient::Request* http_request_{};
  TimeSource& time_source_;

  // The inserted crypto object pointers will not be removed from this map.
  absl::flat_hash_map<std::string, Envoy::Common::Crypto::CryptoObjectPtr> public_key_storage_;
};

/**
 * An empty Callbacks client. It will ignore everything, including successes and failures.
 */
class NoopCallbacks : public Http::AsyncClient::Callbacks {
public:
  // Http::AsyncClient::Callbacks
  void onSuccess(const Http::AsyncClient::Request&, Http::ResponseMessagePtr&&) override {}
  void onFailure(const Http::AsyncClient::Request&, Http::AsyncClient::FailureReason) override {}
  void onBeforeFinalizeUpstreamSpan(Tracing::Span&, const Http::ResponseHeaderMap*) override {}
};

/**
 * Global configuration for the filter.
 */
class FilterConfig : Logger::Loggable<Logger::Id::lua> {
public:
  FilterConfig(const envoy::extensions::filters::http::lua::v3::Lua& proto_config,
               ThreadLocal::SlotAllocator& tls, Upstream::ClusterManager& cluster_manager,
               Api::Api& api, Stats::Scope& scope, const std::string& stat_prefix);

  PerLuaCodeSetup* perLuaCodeSetup(absl::optional<absl::string_view> name = absl::nullopt) const {
    if (!name.has_value()) {
      return default_lua_code_setup_.get();
    }

    const auto iter = per_lua_code_setups_map_.find(name.value());
    if (iter != per_lua_code_setups_map_.end()) {
      return iter->second.get();
    }
    return nullptr;
  }

  const LuaFilterStats& stats() const { return stats_; }

  Upstream::ClusterManager& cluster_manager_;

private:
  LuaFilterStats generateStats(const std::string& prefix, const std::string& filter_stats_prefix,
                               Stats::Scope& scope) {
    const std::string final_prefix = absl::StrCat(prefix, "lua.", filter_stats_prefix);
    return {ALL_LUA_FILTER_STATS(POOL_COUNTER_PREFIX(scope, final_prefix))};
  }

  PerLuaCodeSetupPtr default_lua_code_setup_;
  absl::flat_hash_map<std::string, PerLuaCodeSetupPtr> per_lua_code_setups_map_;
  LuaFilterStats stats_;
};

using FilterConfigConstSharedPtr = std::shared_ptr<FilterConfig>;

/**
 * Route configuration for the filter.
 */
class FilterConfigPerRoute : public Router::RouteSpecificFilterConfig {
public:
  FilterConfigPerRoute(const envoy::extensions::filters::http::lua::v3::LuaPerRoute& config,
                       Server::Configuration::ServerFactoryContext& context);

  ~FilterConfigPerRoute() override {
    // The design of the TLS system does not allow TLS state to be modified in worker threads.
    // However, when the route configuration is dynamically updated via RDS, the old
    // FilterConfigPerRoute object may be destructed in a random worker thread. Therefore, to
    // ensure thread safety, ownership of per_lua_code_setup_ptr_ must be transferred to the main
    // thread and destroyed when the FilterConfigPerRoute object is not destructed in the main
    // thread.
    if (per_lua_code_setup_ptr_ && !main_thread_dispatcher_.isThreadSafe()) {
      auto shared_ptr_wrapper =
          std::make_shared<PerLuaCodeSetupPtr>(std::move(per_lua_code_setup_ptr_));
      main_thread_dispatcher_.post([shared_ptr_wrapper] { shared_ptr_wrapper->reset(); });
    }
  }

  bool disabled() const { return disabled_; }
  const std::string& name() const { return name_; }
  PerLuaCodeSetup* perLuaCodeSetup() const { return per_lua_code_setup_ptr_.get(); }

private:
  Event::Dispatcher& main_thread_dispatcher_;

  const bool disabled_;
  const std::string name_;
  PerLuaCodeSetupPtr per_lua_code_setup_ptr_;
};

namespace {

PerLuaCodeSetup* getPerLuaCodeSetup(const FilterConfig* filter_config,
                                    Http::StreamFilterCallbacks* callbacks) {
  const FilterConfigPerRoute* config_per_route = nullptr;
  if (callbacks && callbacks->route()) {
    config_per_route =
        Http::Utility::resolveMostSpecificPerFilterConfig<FilterConfigPerRoute>(callbacks);
  }

  if (config_per_route != nullptr) {
    if (config_per_route->disabled()) {
      return nullptr;
    }
    if (!config_per_route->name().empty()) {
      ASSERT(filter_config);
      return filter_config->perLuaCodeSetup(config_per_route->name());
    }
    return config_per_route->perLuaCodeSetup();
  }
  ASSERT(filter_config);
  return filter_config->perLuaCodeSetup();
}

} // namespace

// TODO(mattklein123): Filter stats.

/**
 * The HTTP Lua filter. Allows scripts to run in both the request an response flow.
 */
class Filter : public Http::StreamFilter, Logger::Loggable<Logger::Id::lua> {
public:
  Filter(FilterConfigConstSharedPtr config, TimeSource& time_source)
      : config_(config), time_source_(time_source), stats_(config->stats()) {}

  Upstream::ClusterManager& clusterManager() { return config_->cluster_manager_; }
  void scriptError(const Filters::Common::Lua::LuaException& e);
  virtual void scriptLog(spdlog::level::level_enum level, absl::string_view message);

  // Http::StreamFilterBase
  void onDestroy() override;

  // Http::StreamDecoderFilter
  Http::FilterHeadersStatus decodeHeaders(Http::RequestHeaderMap& headers,
                                          bool end_stream) override {
    PerLuaCodeSetup* setup = getPerLuaCodeSetup(config_.get(), decoder_callbacks_.callbacks_);
    const int function_ref = setup ? setup->requestFunctionRef() : LUA_REFNIL;
    return doHeaders(request_stream_wrapper_, request_coroutine_, decoder_callbacks_, function_ref,
                     setup, headers, end_stream);
  }
  Http::FilterDataStatus decodeData(Buffer::Instance& data, bool end_stream) override {
    return doData(request_stream_wrapper_, data, end_stream);
  }
  Http::FilterTrailersStatus decodeTrailers(Http::RequestTrailerMap& trailers) override {
    return doTrailers(request_stream_wrapper_, trailers);
  }
  void setDecoderFilterCallbacks(Http::StreamDecoderFilterCallbacks& callbacks) override {
    decoder_callbacks_.callbacks_ = &callbacks;
  }

  // Http::StreamEncoderFilter
  Http::FilterHeadersStatus encode1xxHeaders(Http::ResponseHeaderMap&) override {
    return Http::FilterHeadersStatus::Continue;
  }
  Http::FilterHeadersStatus encodeHeaders(Http::ResponseHeaderMap& headers,
                                          bool end_stream) override {
    PerLuaCodeSetup* setup = getPerLuaCodeSetup(config_.get(), decoder_callbacks_.callbacks_);
    const int function_ref = setup ? setup->responseFunctionRef() : LUA_REFNIL;
    return doHeaders(response_stream_wrapper_, response_coroutine_, encoder_callbacks_,
                     function_ref, setup, headers, end_stream);
  }
  Http::FilterDataStatus encodeData(Buffer::Instance& data, bool end_stream) override {
    return doData(response_stream_wrapper_, data, end_stream);
  };
  Http::FilterTrailersStatus encodeTrailers(Http::ResponseTrailerMap& trailers) override {
    return doTrailers(response_stream_wrapper_, trailers);
  };
  Http::FilterMetadataStatus encodeMetadata(Http::MetadataMap&) override {
    return Http::FilterMetadataStatus::Continue;
  }
  void setEncoderFilterCallbacks(Http::StreamEncoderFilterCallbacks& callbacks) override {
    encoder_callbacks_.callbacks_ = &callbacks;
  };

private:
  struct DecoderCallbacks : public FilterCallbacks {
    DecoderCallbacks(Filter& parent) : parent_(parent) {}

    // FilterCallbacks
    void addData(Buffer::Instance& data) override {
      return callbacks_->addDecodedData(data, false);
    }
    const Buffer::Instance* bufferedBody() override { return callbacks_->decodingBuffer(); }
    void continueIteration() override { return callbacks_->continueDecoding(); }
    void onHeadersModified() override { callbacks_->downstreamCallbacks()->clearRouteCache(); }
    void respond(Http::ResponseHeaderMapPtr&& headers, Buffer::Instance* body,
                 lua_State* state) override;

    const ProtobufWkt::Struct& metadata() const override;
    StreamInfo::StreamInfo& streamInfo() override { return callbacks_->streamInfo(); }
    const Network::Connection* connection() const override {
      return callbacks_->connection().ptr();
    }
    Tracing::Span& activeSpan() override { return callbacks_->activeSpan(); }

    Filter& parent_;
    Http::StreamDecoderFilterCallbacks* callbacks_{};
  };

  struct EncoderCallbacks : public FilterCallbacks {
    EncoderCallbacks(Filter& parent) : parent_(parent) {}

    // FilterCallbacks
    void addData(Buffer::Instance& data) override {
      return callbacks_->addEncodedData(data, false);
    }
    const Buffer::Instance* bufferedBody() override { return callbacks_->encodingBuffer(); }
    void continueIteration() override { return callbacks_->continueEncoding(); }
    void onHeadersModified() override {}
    void respond(Http::ResponseHeaderMapPtr&& headers, Buffer::Instance* body,
                 lua_State* state) override;

    const ProtobufWkt::Struct& metadata() const override;
    StreamInfo::StreamInfo& streamInfo() override { return callbacks_->streamInfo(); }
    const Network::Connection* connection() const override {
      return callbacks_->connection().ptr();
    }
    Tracing::Span& activeSpan() override { return callbacks_->activeSpan(); }

    Filter& parent_;
    Http::StreamEncoderFilterCallbacks* callbacks_{};
  };

  using StreamHandleRef = Filters::Common::Lua::LuaDeathRef<StreamHandleWrapper>;

  Http::FilterHeadersStatus doHeaders(StreamHandleRef& handle,
                                      Filters::Common::Lua::CoroutinePtr& coroutine,
                                      FilterCallbacks& callbacks, int function_ref,
                                      PerLuaCodeSetup* setup,
                                      Http::RequestOrResponseHeaderMap& headers, bool end_stream);
  Http::FilterDataStatus doData(StreamHandleRef& handle, Buffer::Instance& data, bool end_stream);
  Http::FilterTrailersStatus doTrailers(StreamHandleRef& handle, Http::HeaderMap& trailers);

  FilterConfigConstSharedPtr config_;
  DecoderCallbacks decoder_callbacks_{*this};
  EncoderCallbacks encoder_callbacks_{*this};
  StreamHandleRef request_stream_wrapper_;
  StreamHandleRef response_stream_wrapper_;
  bool destroyed_{};
  TimeSource& time_source_;
  LuaFilterStats stats_;

  // These coroutines used to be owned by the stream handles. After investigating #3570, it
  // became clear that there is a circular memory reference when a coroutine yields. Basically,
  // the coroutine holds a reference to the stream wrapper. I'm not completely sure why this is,
  // but I think it is because the yield happens via a stream handle method, so the runtime must
  // hold a reference so that it can return out of the yield through the object. So now we hold
  // the coroutine references at the same level as the stream handles so that when the filter is
  // destroyed the circular reference is broken and both objects are cleaned up.
  //
  // Note that the above explanation probably means that we don't need to hold a reference to the
  // coroutine at all and it would be taken care of automatically via a runtime internal reference
  // when a yield happens. However, given that I don't fully understand the runtime internals, this
  // seems like a safer fix for now.
  Filters::Common::Lua::CoroutinePtr request_coroutine_;
  Filters::Common::Lua::CoroutinePtr response_coroutine_;
};

} // namespace Lua
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
