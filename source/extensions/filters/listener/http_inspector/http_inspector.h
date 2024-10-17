#pragma once

#include "envoy/event/file_event.h"
#include "envoy/event/timer.h"
#include "envoy/network/filter.h"
#include "envoy/stats/scope.h"
#include "envoy/stats/stats_macros.h"

#include "source/common/common/logger.h"
#include "source/common/http/http1/legacy_parser_impl.h"
#include "source/common/http/http1/parser.h"

#include "absl/container/flat_hash_set.h"

namespace Envoy {
namespace Extensions {
namespace ListenerFilters {
namespace HttpInspector {

/**
 * All stats for the http inspector. @see stats_macros.h
 */
#define ALL_HTTP_INSPECTOR_STATS(COUNTER)                                                          \
  COUNTER(read_error)                                                                              \
  COUNTER(http10_found)                                                                            \
  COUNTER(http11_found)                                                                            \
  COUNTER(http2_found)                                                                             \
  COUNTER(http_not_found)

/**
 * Definition of all stats for the Http inspector. @see stats_macros.h
 */
struct HttpInspectorStats {
  ALL_HTTP_INSPECTOR_STATS(GENERATE_COUNTER_STRUCT)
};

enum class ParseState {
  // Parse result is out. It could be http family or empty.
  Done,
  // Parser expects more data.
  Continue,
  // Parser reports unrecoverable error.
  Error
};

/**
 * Global configuration for http inspector.
 */
class Config {
public:
  Config(Stats::Scope& scope);

  const HttpInspectorStats& stats() const { return stats_; }

  static constexpr uint32_t MAX_INSPECT_SIZE = 8192;

private:
  HttpInspectorStats stats_;
};

class NoOpParserCallbacks : public Http::Http1::ParserCallbacks {
public:
  Http::Http1::CallbackResult onMessageBegin() override {
    return Http::Http1::CallbackResult::Success;
  }

  Http::Http1::CallbackResult onUrl(const char* /*data*/, size_t /*length*/) override {
    return Http::Http1::CallbackResult::Success;
  }

  Http::Http1::CallbackResult onStatus(const char* /*data*/, size_t /*length*/) override {
    return Http::Http1::CallbackResult::Success;
  }

  Http::Http1::CallbackResult onHeaderField(const char* /*data*/, size_t /*length*/) override {
    return Http::Http1::CallbackResult::Success;
  }

  Http::Http1::CallbackResult onHeaderValue(const char* /*data*/, size_t /*length*/) override {
    return Http::Http1::CallbackResult::Success;
  }

  Http::Http1::CallbackResult onHeadersComplete() override {
    return Http::Http1::CallbackResult::Success;
  }

  void bufferBody(const char* /*data*/, size_t /*length*/) override {}

  Http::Http1::CallbackResult onMessageComplete() override {
    return Http::Http1::CallbackResult::Success;
  }

  void onChunkHeader(bool /*is_final_chunk*/) override {}
};

using ConfigSharedPtr = std::shared_ptr<Config>;

/**
 * Http inspector listener filter.
 */
class Filter : public Network::ListenerFilter, Logger::Loggable<Logger::Id::filter> {
public:
  Filter(const ConfigSharedPtr config);

  // Network::ListenerFilter
  Network::FilterStatus onAccept(Network::ListenerFilterCallbacks& cb) override;
  Network::FilterStatus onData(Network::ListenerFilterBuffer& buffer) override;

  size_t maxReadBytes() const override { return Config::MAX_INSPECT_SIZE; }

private:
  static const absl::string_view HTTP2_CONNECTION_PREFACE;

  void done(bool success);
  ParseState parseHttpHeader(absl::string_view data);

  const absl::flat_hash_set<std::string>& httpProtocols() const;
  const absl::flat_hash_set<std::string>& http1xMethods() const;

  ConfigSharedPtr config_;
  Network::ListenerFilterCallbacks* cb_{nullptr};
  absl::string_view protocol_;

  std::unique_ptr<Http::Http1::Parser> parser_;
  NoOpParserCallbacks no_op_callbacks_;
  ssize_t nread_ = 0;
};

} // namespace HttpInspector
} // namespace ListenerFilters
} // namespace Extensions
} // namespace Envoy
