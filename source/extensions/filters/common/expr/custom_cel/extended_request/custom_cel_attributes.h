#pragma once

#include "envoy/http/header_map.h"
#include "envoy/stream_info/stream_info.h"

#include "source/common/http/header_map_impl.h"
#include "source/common/protobuf/protobuf.h"
#include "source/extensions/filters/common/expr/context.h"
#include "source/extensions/filters/common/expr/custom_cel/extended_request/utility/utility.h"

#include "eval/public/cel_value.h"
#include "eval/public/containers/container_backed_list_impl.h"
#include "eval/public/containers/container_backed_map_impl.h"

// Custom Attribute / Activation Value Producer Definitions and Implementations

namespace Envoy {
namespace Extensions {
namespace Filters {
namespace Common {
namespace Expr {
namespace CustomCel {
namespace ExtendedRequest {

// Attribute names for ExtendedRequestWrapper
constexpr absl::string_view Query = "query";

// ExtendedRequestList: keys, returned by ListKeys for ExtendedRequestWrapper
const google::api::expr::runtime::ContainerBackedListImpl ExtendedRequestList{{
    CelValue::CreateStringView(Query),
}};

// ExtendedRequestWrapper contains custom defined keys and all the keys of RequestWrapper.
class ExtendedRequestWrapper : public RequestWrapper {
public:
  ExtendedRequestWrapper(Protobuf::Arena& arena, const Http::RequestHeaderMap* headers,
                         const StreamInfo::StreamInfo& info, bool return_url_query_string_as_map)
      : RequestWrapper(arena, headers, info), arena_(arena),
        return_url_query_string_as_map_(return_url_query_string_as_map),
        request_header_map_(headers) {
    keys_ = Utility::appendList(arena_, RequestWrapper::ListKeys(), &ExtendedRequestList);
  }
  absl::optional<google::api::expr::runtime::CelValue>
  operator[](google::api::expr::runtime::CelValue key) const override;

  const google::api::expr::runtime::CelList* ListKeys() const override { return keys_; }

private:
  absl::optional<CelValue> getMapFromQueryStr(absl::string_view query) const;
  Protobuf::Arena& arena_;
  const bool return_url_query_string_as_map_;
  const Http::RequestHeaderMap* request_header_map_;
  // keys of base class and the derived class
  const google::api::expr::runtime::CelList* keys_;
};

} // namespace ExtendedRequest
} // namespace CustomCel
} // namespace Expr
} // namespace Common
} // namespace Filters
} // namespace Extensions
} // namespace Envoy
