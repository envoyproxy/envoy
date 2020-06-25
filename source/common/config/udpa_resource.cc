#include "common/config/udpa_resource.h"

#include <algorithm>

#include "common/common/fmt.h"
#include "common/http/utility.h"

#include "absl/strings/str_cat.h"
#include "absl/strings/str_split.h"

// TODO(htuch): This file has a bunch of ad hoc URI encoding/decoding based on Envoy's HTTP util
// functions. Once https://github.com/envoyproxy/envoy/issues/6588 lands, we can replace with GURL.

namespace Envoy {
namespace Config {

using PercentEncoding = Http::Utility::PercentEncoding;

std::string UdpaResourceName::encodeUri(const udpa::core::v1::ResourceName& resource_name,
                                        const EncodeOptions& options) {
  // We need to percent-encode authority, id, path and query params. Qualified types should not have
  // reserved characters.
  const std::string authority = PercentEncoding::encode(resource_name.authority(), "%/?#");
  std::vector<std::string> path_components;
  for (const auto& id_component : resource_name.id()) {
    path_components.emplace_back(PercentEncoding::encode(id_component, "%:/?#[]"));
  }
  const std::string path = absl::StrJoin(path_components, "/");
  std::vector<std::string> query_param_components;
  for (const auto& context_param : resource_name.context().params()) {
    query_param_components.emplace_back(
        absl::StrCat(PercentEncoding::encode(context_param.first, "%#[]&="), "=",
                     PercentEncoding::encode(context_param.second, "%#[]&=")));
  }
  if (options.sort_context_params_) {
    std::sort(query_param_components.begin(), query_param_components.end());
  }
  const std::string query_params =
      query_param_components.empty() ? "" : "?" + absl::StrJoin(query_param_components, "&");
  return absl::StrCat("udpa://", authority, "/", resource_name.resource_type(),
                      path.empty() ? "" : "/", path, query_params);
}

udpa::core::v1::ResourceName UdpaResourceName::decodeUri(absl::string_view resource_uri) {
  if (!absl::StartsWith(resource_uri, "udpa:")) {
    throw UdpaResourceName::DecodeException(
        fmt::format("{} does not have an udpa scheme", resource_uri));
  }
  absl::string_view host, path;
  Http::Utility::extractHostPathFromUri(resource_uri, host, path);
  udpa::core::v1::ResourceName decoded_resource_name;
  decoded_resource_name.set_authority(PercentEncoding::decode(host));
  const size_t query_params_start = path.find('?');
  Http::Utility::QueryParams query_params;
  if (query_params_start != absl::string_view::npos) {
    query_params = Http::Utility::parseQueryString(path.substr(query_params_start));
    for (const auto& it : query_params) {
      (*decoded_resource_name.mutable_context()
            ->mutable_params())[PercentEncoding::decode(it.first)] =
          PercentEncoding::decode(it.second);
    }
    path = path.substr(0, query_params_start);
  }
  // This is guaranteed by Http::Utility::extractHostPathFromUri.
  ASSERT(absl::StartsWith(path, "/"));
  const std::vector<absl::string_view> path_components = absl::StrSplit(path.substr(1), '/');
  decoded_resource_name.set_resource_type(std::string(path_components[0]));
  if (decoded_resource_name.resource_type().empty()) {
    throw UdpaResourceName::DecodeException(
        fmt::format("Qualified type missing from {}", resource_uri));
  }
  for (auto it = std::next(path_components.cbegin()); it != path_components.cend(); it++) {
    decoded_resource_name.add_id(PercentEncoding::decode(*it));
  }
  return decoded_resource_name;
}

} // namespace Config
} // namespace Envoy
