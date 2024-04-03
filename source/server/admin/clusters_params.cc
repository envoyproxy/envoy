#include "source/server/admin/clusters_params.h"

#include "envoy/http/codes.h"
#include "envoy/http/query_params.h"

#include "absl/strings/string_view.h"
#include "absl/types/optional.h"

namespace Envoy {
namespace Server {

Http::Code ClustersParams::parse(absl::string_view url, Buffer::Instance& response) {
  UNREFERENCED_PARAMETER(response);

  Http::Utility::QueryParamsMulti query =
      Http::Utility::QueryParamsMulti::parseAndDecodeQueryString(url);
  absl::optional<std::string> optional_format = query.getFirstValue("format");
  if (optional_format.has_value()) {
    if (*optional_format == "text") {
      format_ = Format::Text;
    } else {
      format_ = Format::Json;
    }
  }
  return Http::Code::OK;
}

} // namespace Server
} // namespace Envoy
