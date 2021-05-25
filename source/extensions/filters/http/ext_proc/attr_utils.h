#pragma once

#include <atomic>
#include <map>
#include <memory>

#include "envoy/access_log/access_log.h"
#include "envoy/buffer/buffer.h"
#include "envoy/http/filter.h"
#include "envoy/http/header_map.h"
#include "envoy/stats/sink.h"
#include "envoy/upstream/cluster_manager.h"

#include "common/common/assert.h"
#include "common/common/logger.h"
#include "absl/strings/string_view.h"
#include "google/api/expr/v1alpha1/value.pb.h"

#include "include/envoy/stream_info/_virtual_includes/stream_info_interface/envoy/stream_info/stream_info.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace ExternalProcessing {

using google::api::expr::v1alpha1::Value;

class ExprValueUtil {
public:
  static Value optionalStringValue(const absl::optional<std::string>& str);
  static Value stringValue(const std::string& str);
  static Value int64Value(int64_t n);
  static Value uint64Value(uint64_t n);
  static Value doubleValue(double n);
  static Value boolValue(bool b);
  static const Value nullValue();
};

class AttrUtils : public Logger::Loggable<Logger::Id::filter> {
public:
  AttrUtils(StreamInfo::StreamInfo& info,
            const google::protobuf::RepeatedPtrField<std::string>& specified,
            ProtobufWkt::Map<std::string, ProtobufWkt::Struct>& attributes)
      : info_(info), specified_(specified), attributes_(attributes){};
  ProtobufWkt::Map<std::string, ProtobufWkt::Struct>& build();

  void setRequestHeaders(Envoy::Http::RequestHeaderMap* request_headers);
  void setRequestTrailers(Envoy::Http::RequestTrailerMap* request_trailers);
  void setResponseHeaders(Envoy::Http::ResponseHeaderMap* response_headers);
  void setResponseTrailers(Envoy::Http::ResponseTrailerMap* response_trailers);

private:
  Value findValue(absl::string_view root_tok, absl::string_view sub_tok);
  Value requestSet(absl::string_view name);
  Value responseSet(absl::string_view path);
  Value connectionSet(absl::string_view path);
  Value upstreamSet(absl::string_view path);
  Value sourceSet(absl::string_view path);
  Value destinationSet(absl::string_view path);
  Value metadataSet();
  Value filterStateSet();

  std::tuple<absl::string_view, absl::string_view> tokenizePath(absl::string_view path);
  ProtobufWkt::Map<std::string, ProtobufWkt::Value>& getOrInsert(std::string key);
  std::string getTs();
  std::string formatDuration(absl::Duration duration);
  ProtobufWkt::Value getGrpcStatus();

  StreamInfo::StreamInfo& info_;
  const google::protobuf::RepeatedPtrField<std::string>& specified_;
  ProtobufWkt::Map<std::string, ProtobufWkt::Struct>& attributes_;

  Envoy::Http::RequestHeaderMap* request_headers_;
  Envoy::Http::RequestTrailerMap* request_trailers_;
  Envoy::Http::ResponseHeaderMap* response_headers_;
  Envoy::Http::ResponseTrailerMap* response_trailers_;
};

} // namespace ExternalProcessing
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy