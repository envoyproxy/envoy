#include "source/extensions/filters/common/expr/custom_cel/extended_request/custom_cel_functions.h"

#include "envoy/http/header_map.h"

#include "source/common/http/headers.h"
#include "source/common/http/utility.h"
#include "source/extensions/filters/common/expr/context.h"

#include "eval/public/cel_value.h"
#include "eval/public/containers/container_backed_map_impl.h"

namespace Envoy {
namespace Extensions {
namespace Filters {
namespace Common {
namespace Expr {
namespace CustomCel {
namespace ExtendedRequest {

using Envoy::Extensions::Filters::Common::Expr::CustomCel::ExtendedRequest::Utility::createCelMap;
using google::api::expr::runtime::CelValue;
using google::api::expr::runtime::CreateErrorValue;
using Http::Utility::parseCookies;
using Http::Utility::parseCookieValue;

absl::Status UrlFunction::Evaluate(absl::Span<const CelValue> args, CelValue* output,
                                   Protobuf::Arena* arena) const {
  auto request_header_map = args[0].MapOrDie();
  auto host = (*request_header_map)[CelValue::CreateStringView(Host)];
  if (!host.has_value() || !host->IsString()) {
    *output = CreateErrorValue(arena, "url() host missing", absl::StatusCode::kNotFound);
    return absl::NotFoundError("url() host missing");
  }
  auto path = (*request_header_map)[CelValue::CreateStringView(Path)];
  if (!path.has_value() || !path->IsString()) {
    *output = CreateErrorValue(arena, "url() path missing", absl::StatusCode::kNotFound);
    return absl::NotFoundError("url() path missing");
  }
  std::string url = absl::StrCat(host->StringOrDie().value(), path->StringOrDie().value());
  *output = CelValue::CreateString(Protobuf::Arena::Create<std::string>(arena, url));
  return absl::OkStatus();
}

CelValue cookie(Protobuf::Arena* arena, const Http::RequestHeaderMap& request_header_map) {
  auto cookies = parseCookies(request_header_map);
  if (cookies.empty()) {
    return CreateErrorValue(arena, "cookie() no cookies found", absl::StatusCode::kNotFound);
  }
  return Utility::createCelMap(*arena, cookies);
}

CelValue cookieValue(Protobuf::Arena* arena, const Http::RequestHeaderMap& request_header_map,
                     CelValue key) {
  std::string key_str = *(Protobuf::Arena::Create<std::string>(arena, key.StringOrDie().value()));
  std::string value = parseCookieValue(request_header_map, key_str);
  if (value.empty()) {
    return CreateErrorValue(arena, "cookieValue() cookie value not found",
                            absl::StatusCode::kNotFound);
  }
  return CelValue::CreateString(Protobuf::Arena::Create<std::string>(arena, value));
}

} // namespace ExtendedRequest
} // namespace CustomCel
} // namespace Expr
} // namespace Common
} // namespace Filters
} // namespace Extensions
} // namespace Envoy
