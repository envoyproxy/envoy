#include <atomic>
#include <chrono>
#include <iterator>
#include <map>
#include <memory>
#include <string>

#include "absl/strings/str_format.h"
#include "google/api/expr/v1alpha1/value.pb.h"

#include "envoy/access_log/access_log.h"
#include "envoy/buffer/buffer.h"
#include "envoy/http/filter.h"
#include "envoy/http/header_map.h"
#include "envoy/stats/sink.h"
#include "envoy/upstream/cluster_manager.h"
#include "envoy/network/filter.h"

#include "source/common/grpc/common.h"
#include "source/common/http/headers.h"
#include "source/common/protobuf/utility.h"
#include "source/common/common/assert.h"
#include "source/common/common/logger.h"
#include "source/common/common/fmt.h"
#include "source/common/common/lock_guard.h"
#include "source/common/http/header_map_impl.h"
#include "source/extensions/filters/common/attributes/id.h"

namespace Envoy {
namespace Extensions {
namespace Filters {
namespace Common {
namespace Attributes {

static const std::string HttpProtocolStrings[] = {"Http 1.0", "Http 1.1", "Http 2", "Http 3"};

using HashPolicy = envoy::config::route::v3::RouteAction::HashPolicy;
using MapValue = google::api::expr::v1alpha1::MapValue;
using MapValue_Entry = google::api::expr::v1alpha1::MapValue_Entry;
using Value = google::api::expr::v1alpha1::Value;
using NullValue = google::protobuf::NullValue;
using Any = google::protobuf::Any;

class ValueUtil {
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

class Attributes : public Logger::Loggable<Logger::Id::filter> {
public:
  Attributes(StreamInfo::StreamInfo& stream_info) : stream_info_(stream_info){};

  void setRequestHeaders(Http::RequestHeaderMap* request_headers);
  void setResponseHeaders(Http::ResponseHeaderMap* response_headers);
  void setRequestTrailers(Http::RequestTrailerMap* request_trailers);
  void setResponseTrailers(Http::ResponseTrailerMap* response_trailers);
  absl::optional<Value> getAttribute(AttributeId& attr_id);

private:
  absl::optional<Value> getRequest(AttributeId& attr_id);
  absl::optional<Value> getResponse(AttributeId& attr_id);
  absl::optional<Value> getSource(AttributeId& attr_id);
  absl::optional<Value> getDestination(AttributeId& attr_id);
  absl::optional<Value> getConnection(AttributeId& attr_id);
  absl::optional<Value> getUpstream(AttributeId& attr_id);
  absl::optional<Value> getMetadata();
  absl::optional<Value> getFilterState();

  std::string getTs();
  std::string formatDuration(absl::Duration duration);
  absl::optional<Value> getGrpcStatus();

  StreamInfo::StreamInfo& stream_info_;

  Envoy::Http::RequestHeaderMap* request_headers_;
  Envoy::Http::ResponseHeaderMap* response_headers_;
  Envoy::Http::RequestTrailerMap* request_trailers_;
  Envoy::Http::ResponseTrailerMap* response_trailers_;
};

} // namespace Attributes
} // namespace Common
} // namespace Filters
} // namespace Extensions
} // namespace Envoy