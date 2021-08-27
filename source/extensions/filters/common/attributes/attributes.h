#pragma once

#include <atomic>
#include <chrono>
#include <iterator>
#include <map>
#include <memory>
#include <string>

#include "envoy/access_log/access_log.h"
#include "envoy/buffer/buffer.h"
#include "envoy/http/filter.h"
#include "envoy/http/header_map.h"
#include "envoy/network/filter.h"
#include "envoy/stats/sink.h"
#include "envoy/upstream/cluster_manager.h"

#include "source/common/common/assert.h"
#include "source/common/common/fmt.h"
#include "source/common/common/lock_guard.h"
#include "source/common/common/logger.h"
#include "source/common/grpc/common.h"
#include "source/common/http/header_map_impl.h"
#include "source/common/http/headers.h"
#include "source/common/protobuf/utility.h"
#include "source/extensions/filters/common/attributes/id.h"

#include "absl/strings/str_format.h"
#include "google/api/expr/v1alpha1/value.pb.h"

namespace Envoy {
namespace Extensions {
namespace Filters {
namespace Common {
namespace Attributes {

static const std::string HttpProtocolStrings[] = {"Http 1.0", "Http 1.1", "Http 2", "Http 3"};

class ValueUtil : public Logger::Loggable<Logger::Id::filter> {
public:
  /**
   * Returns a "null" `Value`.
   */
  static google::api::expr::v1alpha1::Value* nullValue();

  /**
   * Wraps a int64 and returns a pointer to the `Value`.
   */
  static google::api::expr::v1alpha1::Value* int64Value(int64_t n);

  /**
   * Wraps a uint64 and returns a pointer to the `Value`.
   */
  static google::api::expr::v1alpha1::Value* uint64Value(uint64_t n);

  /**
   * Wraps a double and returns a pointer to the `Value`.
   */
  static google::api::expr::v1alpha1::Value* doubleValue(double n);

  /**
   * Wraps a bool and returns a pointer to the `Value`.
   */
  static google::api::expr::v1alpha1::Value* boolValue(bool b);

  /**
   * Makes a copy of the string, a pointer to the wrapping `Value` is returned.
   */
  static google::api::expr::v1alpha1::Value* stringValue(const std::string& str);

  /**
   * Wraps a value `T` in a `Value` whose pointer is returned.
   */
  template <class T> static google::api::expr::v1alpha1::Value* objectValue(T val);

  /**
   * Get the entry in the given map or insert one with the given key.
   */
  static google::api::expr::v1alpha1::MapValue_Entry*
  getOrInsert(google::api::expr::v1alpha1::MapValue* m, absl::string_view key);
};

class Attributes : public Logger::Loggable<Logger::Id::filter> {
public:
  Attributes(StreamInfo::StreamInfo& stream_info) : stream_info_(stream_info){};

  void setRequestHeaders(const Http::RequestHeaderMap* request_headers) {
    request_headers_ = request_headers;
  }
  void setResponseHeaders(const Http::ResponseHeaderMap* response_headers) {
    response_headers_ = response_headers;
  }
  void setRequestTrailers(const Http::RequestTrailerMap* request_trailers) {
    request_trailers_ = request_trailers;
  }
  void setResponseTrailers(const Http::ResponseTrailerMap* response_trailers) {
    response_trailers_ = response_trailers;
  }
  google::api::expr::v1alpha1::Value* buildAttributesValue(const std::vector<AttributeId>& attrs);

  // these should be private
  google::api::expr::v1alpha1::Value* get(AttributeId& attr_id);
  google::api::expr::v1alpha1::Value* get(RequestToken tok);
  google::api::expr::v1alpha1::Value* get(ResponseToken tok);
  google::api::expr::v1alpha1::Value* get(SourceToken tok);
  google::api::expr::v1alpha1::Value* get(DestinationToken tok);
  google::api::expr::v1alpha1::Value* get(ConnectionToken tok);
  google::api::expr::v1alpha1::Value* get(UpstreamToken tok);
  google::api::expr::v1alpha1::Value* full(RootToken tok);
  google::api::expr::v1alpha1::Value* getRequestHeaders();

private:
  template <class T>
  google::api::expr::v1alpha1::Value* full(absl::flat_hash_map<std::string, T>& tokens);
  google::api::expr::v1alpha1::Value* getMetadata();
  google::api::expr::v1alpha1::Value* getFilterState();
  google::api::expr::v1alpha1::Value* getResponseHeaders();
  google::api::expr::v1alpha1::Value* getResponseTrailers();
  google::api::expr::v1alpha1::Value* getResponseGrpcStatus();

  std::string getTs();
  std::string formatDuration(absl::Duration duration);
  absl::optional<google::api::expr::v1alpha1::Value> getGrpcStatus();

  StreamInfo::StreamInfo& stream_info_;

  const Envoy::Http::RequestHeaderMap* request_headers_ = nullptr;
  const Envoy::Http::ResponseHeaderMap* response_headers_ = nullptr;
  const Envoy::Http::RequestTrailerMap* request_trailers_ = nullptr;
  const Envoy::Http::ResponseTrailerMap* response_trailers_ = nullptr;
};

} // namespace Attributes
} // namespace Common
} // namespace Filters
} // namespace Extensions
} // namespace Envoy
