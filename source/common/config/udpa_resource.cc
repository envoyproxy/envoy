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

namespace {

// We need to percent-encode authority, id, path and query params. Resource types should not have
// reserved characters.

std::string encodeAuthority(const std::string& authority) {
  return PercentEncoding::encode(authority, "%/?#");
}

std::string encodeIdPath(const Protobuf::RepeatedPtrField<std::string>& id) {
  std::vector<std::string> path_components;
  for (const auto& id_component : id) {
    path_components.emplace_back(PercentEncoding::encode(id_component, "%:/?#[]"));
  }
  const std::string path = absl::StrJoin(path_components, "/");
  return path.empty() ? "" : absl::StrCat("/", path);
}

std::string encodeContextParams(const udpa::core::v1::ContextParams& context_params,
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
    const Protobuf::RepeatedPtrField<udpa::core::v1::ResourceLocator::Directive>& directives) {
  std::vector<std::string> fragment_components;
  const std::string DirectiveEscapeChars = "%#[],";
  for (const auto& directive : directives) {
    switch (directive.directive_case()) {
    case udpa::core::v1::ResourceLocator::Directive::DirectiveCase::kAlt:
      fragment_components.emplace_back(absl::StrCat(
          "alt=", PercentEncoding::encode(UdpaResourceIdentifier::encodeUrl(directive.alt()),
                                          DirectiveEscapeChars)));
      break;
    case udpa::core::v1::ResourceLocator::Directive::DirectiveCase::kEntry:
      fragment_components.emplace_back(
          absl::StrCat("entry=", PercentEncoding::encode(directive.entry(), DirectiveEscapeChars)));
      break;
    default:
      NOT_REACHED_GCOVR_EXCL_LINE;
    }
  }
  return fragment_components.empty() ? "" : "#" + absl::StrJoin(fragment_components, ",");
}

} // namespace

std::string UdpaResourceIdentifier::encodeUrn(const udpa::core::v1::ResourceName& resource_name,
                                              const EncodeOptions& options) {
  const std::string authority = encodeAuthority(resource_name.authority());
  const std::string id_path = encodeIdPath(resource_name.id());
  const std::string query_params =
      encodeContextParams(resource_name.context(), options.sort_context_params_);
  return absl::StrCat("udpa://", authority, "/", resource_name.resource_type(), id_path,
                      query_params);
}

std::string
UdpaResourceIdentifier::encodeUrl(const udpa::core::v1::ResourceLocator& resource_locator,
                                  const EncodeOptions& options) {
  const std::string id_path = encodeIdPath(resource_locator.id());
  const std::string fragment = encodeDirectives(resource_locator.directives());
  std::string scheme = "udpa:";
  switch (resource_locator.scheme()) {
  case udpa::core::v1::ResourceLocator::HTTP:
    scheme = "http:";
    FALLTHRU;
  case udpa::core::v1::ResourceLocator::UDPA: {
    const std::string authority = encodeAuthority(resource_locator.authority());
    const std::string query_params =
        encodeContextParams(resource_locator.exact_context(), options.sort_context_params_);
    return absl::StrCat(scheme, "//", authority, "/", resource_locator.resource_type(), id_path,
                        query_params, fragment);
  }
  case udpa::core::v1::ResourceLocator::FILE: {
    return absl::StrCat("file://", id_path, fragment);
  }
  default:
    NOT_REACHED_GCOVR_EXCL_LINE;
  }
}

namespace {

void decodePath(absl::string_view path, std::string* resource_type,
                Protobuf::RepeatedPtrField<std::string>& id) {
  // This is guaranteed by Http::Utility::extractHostPathFromUrn.
  ASSERT(absl::StartsWith(path, "/"));
  const std::vector<absl::string_view> path_components = absl::StrSplit(path.substr(1), '/');
  auto id_it = path_components.cbegin();
  if (resource_type != nullptr) {
    *resource_type = std::string(path_components[0]);
    if (resource_type->empty()) {
      throw UdpaResourceIdentifier::DecodeException(
          fmt::format("Resource type missing from {}", path));
    }
    id_it = std::next(id_it);
  }
  for (; id_it != path_components.cend(); id_it++) {
    *id.Add() = PercentEncoding::decode(*id_it);
  }
}

void decodeQueryParams(absl::string_view query_params,
                       udpa::core::v1::ContextParams& context_params) {
  Http::Utility::QueryParams query_params_components =
      Http::Utility::parseQueryString(query_params);
  for (const auto& it : query_params_components) {
    (*context_params.mutable_params())[PercentEncoding::decode(it.first)] =
        PercentEncoding::decode(it.second);
  }
}

void decodeFragment(
    absl::string_view fragment,
    Protobuf::RepeatedPtrField<udpa::core::v1::ResourceLocator::Directive>& directives) {
  const std::vector<absl::string_view> fragment_components = absl::StrSplit(fragment, ',');
  for (const absl::string_view& fragment_component : fragment_components) {
    if (absl::StartsWith(fragment_component, "alt=")) {
      directives.Add()->mutable_alt()->MergeFrom(
          UdpaResourceIdentifier::decodeUrl(PercentEncoding::decode(fragment_component.substr(4))));
    } else if (absl::StartsWith(fragment_component, "entry=")) {
      directives.Add()->set_entry(PercentEncoding::decode(fragment_component.substr(6)));
    } else {
      throw UdpaResourceIdentifier::DecodeException(
          fmt::format("Unknown fragment component {}", fragment_component));
      ;
    }
  }
}

} // namespace

udpa::core::v1::ResourceName UdpaResourceIdentifier::decodeUrn(absl::string_view resource_urn) {
  if (!absl::StartsWith(resource_urn, "udpa:")) {
    throw UdpaResourceIdentifier::DecodeException(
        fmt::format("{} does not have an udpa: scheme", resource_urn));
  }
  absl::string_view host, path;
  Http::Utility::extractHostPathFromUri(resource_urn, host, path);
  udpa::core::v1::ResourceName decoded_resource_name;
  decoded_resource_name.set_authority(PercentEncoding::decode(host));
  const size_t query_params_start = path.find('?');
  if (query_params_start != absl::string_view::npos) {
    decodeQueryParams(path.substr(query_params_start), *decoded_resource_name.mutable_context());
    path = path.substr(0, query_params_start);
  }
  decodePath(path, decoded_resource_name.mutable_resource_type(),
             *decoded_resource_name.mutable_id());
  return decoded_resource_name;
}

udpa::core::v1::ResourceLocator UdpaResourceIdentifier::decodeUrl(absl::string_view resource_url) {
  absl::string_view host, path;
  Http::Utility::extractHostPathFromUri(resource_url, host, path);
  udpa::core::v1::ResourceLocator decoded_resource_locator;
  const size_t fragment_start = path.find('#');
  if (fragment_start != absl::string_view::npos) {
    decodeFragment(path.substr(fragment_start + 1), *decoded_resource_locator.mutable_directives());
    path = path.substr(0, fragment_start);
  }
  if (absl::StartsWith(resource_url, "udpa:")) {
    decoded_resource_locator.set_scheme(udpa::core::v1::ResourceLocator::UDPA);
  } else if (absl::StartsWith(resource_url, "http:")) {
    decoded_resource_locator.set_scheme(udpa::core::v1::ResourceLocator::HTTP);
  } else if (absl::StartsWith(resource_url, "file:")) {
    decoded_resource_locator.set_scheme(udpa::core::v1::ResourceLocator::FILE);
    // File URLs only have a path and fragment.
    decodePath(path, nullptr, *decoded_resource_locator.mutable_id());
    return decoded_resource_locator;
  } else {
    throw UdpaResourceIdentifier::DecodeException(
        fmt::format("{} does not have a udpa:, http: or file: scheme", resource_url));
  }
  decoded_resource_locator.set_authority(PercentEncoding::decode(host));
  const size_t query_params_start = path.find('?');
  if (query_params_start != absl::string_view::npos) {
    decodeQueryParams(path.substr(query_params_start),
                      *decoded_resource_locator.mutable_exact_context());
    path = path.substr(0, query_params_start);
  }
  decodePath(path, decoded_resource_locator.mutable_resource_type(),
             *decoded_resource_locator.mutable_id());
  return decoded_resource_locator;
}

} // namespace Config
} // namespace Envoy
