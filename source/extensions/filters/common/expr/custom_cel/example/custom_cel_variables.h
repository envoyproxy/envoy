#pragma once

#include "envoy/http/header_map.h"
#include "envoy/stream_info/stream_info.h"

#include "source/common/http/header_map_impl.h"
#include "source/common/protobuf/protobuf.h"
#include "source/extensions/filters/common/expr/context.h"
#include "source/extensions/filters/common/expr/custom_cel/example/util.h"

#include "eval/public/cel_value.h"
#include "eval/public/containers/container_backed_list_impl.h"
#include "eval/public/containers/container_backed_map_impl.h"

// Custom Variable Set / Activation Value Producer Definitions and Implementations

namespace Envoy {
namespace Extensions {
namespace Filters {
namespace Common {
namespace Expr {
namespace Custom_CEL {
namespace Example {

using Envoy::Extensions::Filters::Common::Expr::Custom_CEL::Example::Utility;
using google::api::expr::runtime::CelList;
using google::api::expr::runtime::ContainerBackedListImpl;

// variable names for CustomWrapper
constexpr absl::string_view Team = "team";
constexpr absl::string_view Protocol = "protocol";

// CustomList: keys, returned by ListKeys for CustomWrapper
const ContainerBackedListImpl CustomList{{
    CelValue::CreateStringView(Team),
    CelValue::CreateStringView(Protocol),
}};

// variable names for SourceWrapper
constexpr absl::string_view Address = "address";
constexpr absl::string_view Port = "port";
constexpr absl::string_view Description = "description";

// SourceList: keys, returned by ListKeys for SourceWrapper
const ContainerBackedListImpl SourceList{{
    CelValue::CreateStringView(Address),
    CelValue::CreateStringView(Port),
    CelValue::CreateStringView(Description),
}};

// variable names for ExtendedRequestWrapper
constexpr absl::string_view Query = "query";

// ExtendedRequestList: keys, returned by ListKeys for ExtendedRequestWrapper
const ContainerBackedListImpl ExtendedRequestList{{
    CelValue::CreateStringView(Query),
}};

class CustomWrapper : public BaseWrapper {
public:
  CustomWrapper(Protobuf::Arena& arena, const StreamInfo::StreamInfo& info)
      : arena_(arena), info_(info) {}

  absl::optional<CelValue> operator[](CelValue key) const override;

  const CelList* ListKeys() const override { return &CustomList; }

private:
  Protobuf::Arena& arena_;
  const StreamInfo::StreamInfo& info_;
};

// SourceWrapper extends PeerWrapper(with local=false)
// SourceWrapper contains custom defined keys and all the keys of PeerWrapper (with locazl=false).
class SourceWrapper : public PeerWrapper {
public:
  SourceWrapper(Protobuf::Arena& arena, const StreamInfo::StreamInfo& info)
      : PeerWrapper(info, false), arena_(arena) {
    auto base_class_keys = dynamic_cast<const ContainerBackedListImpl*>(PeerWrapper::ListKeys());
    keys_ = Utility::appendList(arena_, base_class_keys, &SourceList);
  }

  absl::optional<CelValue> operator[](CelValue key) const override;

  const CelList* ListKeys() const override { return keys_; }

private:
  Protobuf::Arena& arena_;
  // keys of base class and the derived class
  const ContainerBackedListImpl* keys_;
};

// ExtendedRequestWrapper extends RequestWrapper
// ExtendedRequestWrapper contains custom defined keys and all the keys of RequestWrapper.
class ExtendedRequestWrapper : public RequestWrapper {
public:
  ExtendedRequestWrapper(Protobuf::Arena& arena, const Http::RequestHeaderMap* headers,
                         const StreamInfo::StreamInfo& info, bool return_url_query_string_as_map)
      : RequestWrapper(arena, headers, info), arena_(arena),
        return_url_query_string_as_map_(return_url_query_string_as_map),
        request_header_map_(headers) {
    auto base_class_keys = dynamic_cast<const ContainerBackedListImpl*>(RequestWrapper::ListKeys());
    keys_ = Utility::appendList(arena_, base_class_keys, &ExtendedRequestList);
  }
  absl::optional<CelValue> operator[](CelValue key) const override;

  const CelList* ListKeys() const override { return keys_; }

private:
  absl::optional<CelValue> getMapFromQueryStr(absl::string_view query) const;
  Protobuf::Arena& arena_;
  const bool return_url_query_string_as_map_;
  const Http::RequestHeaderMap* request_header_map_;
  // keys of base class and the derived class
  const ContainerBackedListImpl* keys_;
};

} // namespace Example
} // namespace Custom_CEL
} // namespace Expr
} // namespace Common
} // namespace Filters
} // namespace Extensions
} // namespace Envoy
