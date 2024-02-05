#include "source/common/config/xds_resource.h"

#include <algorithm>

#include "source/common/common/fmt.h"
#include "source/common/http/utility.h"

#include "absl/strings/str_cat.h"
#include "absl/strings/str_split.h"

// TODO(htuch): This file has a bunch of ad hoc URI encoding/decoding based on Envoy's HTTP util
// functions. Once https://github.com/envoyproxy/envoy/issues/6588 lands, we can replace with GURL.

namespace Envoy {
namespace Config {

using PercentEncoding = Http::Utility::PercentEncoding;

namespace {

// We need to percent-encode authority, id, path and query params. Resource types should not have
// reserved characters.

std::string encodeAuthority(const std::string& authority) {
  return PercentEncoding::encode(authority, "%/?#");
}

std::string encodeIdPath(const std::string& id) {
  const std::string path = PercentEncoding::encode(id, "%:?#[]");
  return path.empty() ? "" : absl::StrCat("/", path);
}

std::string encodeContextParams(const xds::core::v3::ContextParams& context_params,
                                bool sort_context_params) {
  std::vector<std::string> query_param_components;
  for (const auto& context_param : context_params.params()) {
    query_param_components.emplace_back(
        absl::StrCat(PercentEncoding::encode(context_param.first, "%#[]&="), "=",
                     PercentEncoding::encode(context_param.second, "%#[]&=")));
  }
  if (sort_context_params) {
    std::sort(query_param_components.begin(), query_param_components.end());
  }
  return query_param_components.empty() ? "" : "?" + absl::StrJoin(query_param_components, "&");
}

std::string encodeDirectives(
    const Protobuf::RepeatedPtrField<xds::core::v3::ResourceLocator::Directive>& directives) {
  std::vector<std::string> fragment_components;
  const std::string DirectiveEscapeChars = "%#[],";
  for (const auto& directive : directives) {
    switch (directive.directive_case()) {
    case xds::core::v3::ResourceLocator::Directive::DirectiveCase::kAlt:
      fragment_components.emplace_back(absl::StrCat(
          "alt=", PercentEncoding::encode(XdsResourceIdentifier::encodeUrl(directive.alt()),
                                          DirectiveEscapeChars)));
      break;
    case xds::core::v3::ResourceLocator::Directive::DirectiveCase::kEntry:
      fragment_components.emplace_back(
          absl::StrCat("entry=", PercentEncoding::encode(directive.entry(), DirectiveEscapeChars)));
      break;
    case xds::core::v3::ResourceLocator::Directive::DirectiveCase::DIRECTIVE_NOT_SET:
      PANIC_DUE_TO_PROTO_UNSET;
    }
  }
  return fragment_components.empty() ? "" : "#" + absl::StrJoin(fragment_components, ",");
}

} // namespace

std::string XdsResourceIdentifier::encodeUrn(const xds::core::v3::ResourceName& resource_name,
                                             const EncodeOptions& options) {
  const std::string authority = encodeAuthority(resource_name.authority());
  const std::string id_path = encodeIdPath(resource_name.id());
  const std::string query_params =
      encodeContextParams(resource_name.context(), options.sort_context_params_);
  return absl::StrCat("xdstp://", authority, "/", resource_name.resource_type(), id_path,
                      query_params);
}

std::string XdsResourceIdentifier::encodeUrl(const xds::core::v3::ResourceLocator& resource_locator,
                                             const EncodeOptions& options) {
  const std::string id_path = encodeIdPath(resource_locator.id());
  const std::string fragment = encodeDirectives(resource_locator.directives());
  std::string scheme = "xdstp:";
  switch (resource_locator.scheme()) {
    PANIC_ON_PROTO_ENUM_SENTINEL_VALUES;
  case xds::core::v3::ResourceLocator::HTTP:
    scheme = "http:";
    FALLTHRU;
  case xds::core::v3::ResourceLocator::XDSTP: {
    const std::string authority = encodeAuthority(resource_locator.authority());
    const std::string query_params =
        encodeContextParams(resource_locator.exact_context(), options.sort_context_params_);
    return absl::StrCat(scheme, "//", authority, "/", resource_locator.resource_type(), id_path,
                        query_params, fragment);
  }
  case xds::core::v3::ResourceLocator::FILE: {
    return absl::StrCat("file://", id_path, fragment);
  }
  }
  return "";
}

namespace {

absl::Status decodePath(absl::string_view path, std::string* resource_type, std::string& id) {
  // This is guaranteed by Http::Utility::extractHostPathFromUrn.
  ASSERT(absl::StartsWith(path, "/"));
  const std::vector<absl::string_view> path_components = absl::StrSplit(path.substr(1), '/');
  auto id_it = path_components.cbegin();
  if (resource_type != nullptr) {
    *resource_type = std::string(path_components[0]);
    if (resource_type->empty()) {
      return absl::InvalidArgumentError(fmt::format("Resource type missing from {}", path));
    }
    id_it = std::next(id_it);
  }
  id = PercentEncoding::decode(absl::StrJoin(id_it, path_components.cend(), "/"));
  return absl::OkStatus();
}

void decodeQueryParams(absl::string_view query_params,
                       xds::core::v3::ContextParams& context_params) {
  auto query_params_components = Http::Utility::QueryParamsMulti::parseQueryString(query_params);
  for (const auto& it : query_params_components.data()) {
    (*context_params.mutable_params())[PercentEncoding::decode(it.first)] =
        PercentEncoding::decode(it.second[0]);
  }
}

absl::Status
decodeFragment(absl::string_view fragment,
               Protobuf::RepeatedPtrField<xds::core::v3::ResourceLocator::Directive>& directives) {
  const std::vector<absl::string_view> fragment_components = absl::StrSplit(fragment, ',');
  for (const absl::string_view& fragment_component : fragment_components) {
    if (absl::StartsWith(fragment_component, "alt=")) {
      directives.Add()->mutable_alt()->MergeFrom(
          XdsResourceIdentifier::decodeUrl(PercentEncoding::decode(fragment_component.substr(4))));
    } else if (absl::StartsWith(fragment_component, "entry=")) {
      directives.Add()->set_entry(PercentEncoding::decode(fragment_component.substr(6)));
    } else {
      return absl::InvalidArgumentError(
          fmt::format("Unknown fragment component {}", fragment_component));
    }
  }
  return absl::OkStatus();
}

} // namespace

absl::StatusOr<xds::core::v3::ResourceName>
XdsResourceIdentifier::decodeUrn(absl::string_view resource_urn) {
  if (!hasXdsTpScheme(resource_urn)) {
    return absl::InvalidArgumentError(
        fmt::format("{} does not have an xdstp: scheme", resource_urn));
  }
  absl::string_view host, path;
  Http::Utility::extractHostPathFromUri(resource_urn, host, path);
  xds::core::v3::ResourceName decoded_resource_name;
  decoded_resource_name.set_authority(PercentEncoding::decode(host));
  const size_t query_params_start = path.find('?');
  if (query_params_start != absl::string_view::npos) {
    decodeQueryParams(path.substr(query_params_start), *decoded_resource_name.mutable_context());
    path = path.substr(0, query_params_start);
  }
  auto status = decodePath(path, decoded_resource_name.mutable_resource_type(),
                           *decoded_resource_name.mutable_id());
  if (!status.ok()) {
    return status;
  }
  return decoded_resource_name;
}

xds::core::v3::ResourceLocator XdsResourceIdentifier::decodeUrl(absl::string_view resource_url) {
  absl::string_view host, path;
  Http::Utility::extractHostPathFromUri(resource_url, host, path);
  xds::core::v3::ResourceLocator decoded_resource_locator;
  const size_t fragment_start = path.find('#');
  if (fragment_start != absl::string_view::npos) {
    THROW_IF_NOT_OK(decodeFragment(path.substr(fragment_start + 1),
                                   *decoded_resource_locator.mutable_directives()));
    path = path.substr(0, fragment_start);
  }
  if (hasXdsTpScheme(resource_url)) {
    decoded_resource_locator.set_scheme(xds::core::v3::ResourceLocator::XDSTP);
  } else if (absl::StartsWith(resource_url, "http:")) {
    decoded_resource_locator.set_scheme(xds::core::v3::ResourceLocator::HTTP);
  } else if (absl::StartsWith(resource_url, "file:")) {
    decoded_resource_locator.set_scheme(xds::core::v3::ResourceLocator::FILE);
    // File URLs only have a path and fragment.
    THROW_IF_NOT_OK(decodePath(path, nullptr, *decoded_resource_locator.mutable_id()));
    return decoded_resource_locator;
  } else {
    throwEnvoyExceptionOrPanic(
        fmt::format("{} does not have a xdstp:, http: or file: scheme", resource_url));
  }
  decoded_resource_locator.set_authority(PercentEncoding::decode(host));
  const size_t query_params_start = path.find('?');
  if (query_params_start != absl::string_view::npos) {
    decodeQueryParams(path.substr(query_params_start),
                      *decoded_resource_locator.mutable_exact_context());
    path = path.substr(0, query_params_start);
  }
  THROW_IF_NOT_OK(decodePath(path, decoded_resource_locator.mutable_resource_type(),
                             *decoded_resource_locator.mutable_id()));
  return decoded_resource_locator;
}

bool XdsResourceIdentifier::hasXdsTpScheme(absl::string_view resource_name) {
  return absl::StartsWith(resource_name, "xdstp:");
}

} // namespace Config
} // namespace Envoy
