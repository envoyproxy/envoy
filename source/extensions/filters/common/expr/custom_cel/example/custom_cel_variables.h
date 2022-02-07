#pragma once

#include "envoy/http/header_map.h"
#include "envoy/stream_info/stream_info.h"

#include "source/common/protobuf/protobuf.h"
#include "source/extensions/filters/common/expr/context.h"

#include "eval/public/cel_value.h"

// Toy variables for the example custom cel vocabulary

namespace Envoy {
namespace Extensions {
namespace Filters {
namespace Common {
namespace Expr {
namespace Custom_Cel {
namespace Example {

using ContainerBackedListImpl = google::api::expr::runtime::ContainerBackedListImpl;

constexpr absl::string_view Team = "team";
constexpr absl::string_view Protocol = "protocol";

const ContainerBackedListImpl CustomList{{
    CelValue::CreateStringView(Team),
    CelValue::CreateStringView(Protocol),
}};

constexpr absl::string_view Address = "address";
constexpr absl::string_view Port = "port";
constexpr absl::string_view Description = "description";

const ContainerBackedListImpl SourceList{{
    CelValue::CreateStringView(Address),
    CelValue::CreateStringView(Port),
    CelValue::CreateStringView(Description),
}};

class CustomWrapper : public BaseWrapper {
public:
  CustomWrapper(Protobuf::Arena& arena, const StreamInfo::StreamInfo& info)
      : arena_(arena), info_(info) {}

  absl::optional<CelValue> operator[](CelValue key) const override;

  const google::api::expr::runtime::CelList* ListKeys() const override { return &CustomList; }

private:
  Protobuf::Arena& arena_;
  const StreamInfo::StreamInfo& info_;
};

class SourceWrapper : public BaseWrapper {
public:
  SourceWrapper(Protobuf::Arena& arena, const StreamInfo::StreamInfo& info)
      : arena_(arena), info_(info) {}

  absl::optional<CelValue> operator[](CelValue key) const override;

  const google::api::expr::runtime::CelList* ListKeys() const override { return &SourceList; }

private:
  Protobuf::Arena& arena_;
  const StreamInfo::StreamInfo& info_;
};

} // namespace Example
} // namespace Custom_Cel
} // namespace Expr
} // namespace Common
} // namespace Filters
} // namespace Extensions
} // namespace Envoy
