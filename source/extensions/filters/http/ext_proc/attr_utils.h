#pragma once

#include <atomic>
#include <map>
#include <memory>

#include "client.h"
#include "envoy/access_log/access_log.h"
#include "envoy/buffer/buffer.h"
#include "envoy/http/filter.h"
#include "envoy/http/header_map.h"
#include "envoy/stats/sink.h"
#include "envoy/upstream/cluster_manager.h"

#include "source/common/common/assert.h"
#include "source/common/common/logger.h"
#include "absl/strings/string_view.h"
#include "google/api/expr/v1alpha1/value.pb.h"
#include "google/protobuf/any.pb.h"

#include "envoy/stream_info/stream_info.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace ExternalProcessing {

using google::api::expr::v1alpha1::MapValue;
using google::api::expr::v1alpha1::Value;
using google::protobuf::Any;

class ExprValueUtil {
public:
  static Value optionalStringValue(const absl::optional<std::string>& str);
  static Value stringValue(const std::string& str);
  static Value int64Value(int64_t n);
  static Value uint64Value(uint64_t n);
  static Value doubleValue(double n);
  static Value boolValue(bool b);
  static Value objectValue(Any o);
  static Value mapValue(MapValue* m);
  static const Value nullValue();
};

class AttrUtils {
public:
  std::vector<std::tuple<absl::string_view, absl::string_view>>
  tokenizeAttrs(const google::protobuf::RepeatedPtrField<std::string> attrs);
  static std::tuple<absl::string_view, absl::string_view> tokenizeAttrPath(absl::string_view path);
}

class AttrState : public Logger::Loggable<Logger::Id::filter> {
public:
  AttrState(StreamInfo::StreamInfo& stream_info,
            std::vector<std::tuple<absl::string_view, absl::string_view>>& specified)
      : stream_info_(info), specified_(specified){};
  void populateRequestAttributes(Envoy::Http::RequestHeaderMap& headers,
                                 ProtobufWkt::Map<std::string, ProtobufWkt::Struct>& attrs);
  void populateResponseAttributes(Envoy::Http::ResponseHeaderMap& headers,
                                  ProtobufWkt::Map<std::string, ProtobufWkt::Struct>& attrs);

private:
  absl::optional<Value> findValue(absl::string_view root_tok, absl::string_view sub_tok);
  absl::optional<Value> requestSet(absl::string_view name);
  absl::optional<Value> responseSet(absl::string_view path);
  absl::optional<Value> connectionSet(absl::string_view path);
  absl::optional<Value> upstreamSet(absl::string_view path);
  absl::optional<Value> sourceSet(absl::string_view path);
  absl::optional<Value> destinationSet(absl::string_view path);
  absl::optional<Value> metadataSet();
  absl::optional<Value> filterStateSet();

  ProtobufWkt::Map<std::string, ProtobufWkt::Value>& getOrInsert(std::string key);
  std::string getTs();
  std::string formatDuration(absl::Duration duration);
  absl::optional<Value> getGrpcStatus();

  StreamInfo::StreamInfo& stream_info_;
  std::vector<std::tuple<absl::string_view, absl::string_view>> specified_;
};

} // namespace ExternalProcessing
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy