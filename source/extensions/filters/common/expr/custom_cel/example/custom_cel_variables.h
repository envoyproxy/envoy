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

constexpr absl::string_view VariableTeam = "team";
constexpr absl::string_view VariableProtocol = "protocol";

const ContainerBackedListImpl CustomCelVariablesList{{
    CelValue::CreateStringView(VariableTeam),
    CelValue::CreateStringView(VariableProtocol),
}};

class CustomCelVariablesWrapper : public BaseWrapper {
public:
  CustomCelVariablesWrapper(Protobuf::Arena& arena, const StreamInfo::StreamInfo& info,
                            const Http::RequestHeaderMap* request_headers,
                            const Http::ResponseHeaderMap* response_headers,
                            const Http::ResponseTrailerMap* response_trailers)
      : arena_(arena), info_(info), request_headers_(request_headers),
        response_headers_(response_headers), response_trailers_(response_trailers) {}

  absl::optional<CelValue> operator[](CelValue key) const override;

  const google::api::expr::runtime::CelList* ListKeys() const override {
    return &CustomCelVariablesList;
  }

  const Http::RequestHeaderMap* requestHeaders() { return request_headers_; }
  const Http::ResponseHeaderMap* responseHeaders() { return response_headers_; }
  const Http::ResponseTrailerMap* responseTrailers() { return response_trailers_; }

private:
  Protobuf::Arena& arena_;
  const StreamInfo::StreamInfo& info_;
  const Http::RequestHeaderMap* request_headers_;
  const Http::ResponseHeaderMap* response_headers_;
  const Http::ResponseTrailerMap* response_trailers_;
};

} // namespace Example
} // namespace Custom_Cel
} // namespace Expr
} // namespace Common
} // namespace Filters
} // namespace Extensions
} // namespace Envoy
