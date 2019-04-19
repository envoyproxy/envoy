#include "extensions/filters/http/common/aws/utility.h"

#include "common/common/fmt.h"
#include "common/common/utility.h"

#include "absl/strings/str_join.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace Common {
namespace Aws {

std::map<std::string, std::string> Utility::canonicalizeHeaders(const Http::HeaderMap& headers) {
  std::map<std::string, std::string> out;
  headers.iterate(
      [](const Http::HeaderEntry& entry, void* context) -> Http::HeaderMap::Iterate {
        auto* map = static_cast<std::map<std::string, std::string>*>(context);
        // Skip empty headers
        if (entry.key().empty() || entry.value().empty()) {
          return Http::HeaderMap::Iterate::Continue;
        }
        // Pseudo-headers should not be canonicalized
        if (!entry.key().getStringView().empty() && entry.key().getStringView()[0] == ':') {
          return Http::HeaderMap::Iterate::Continue;
        }
        std::string value(entry.value().getStringView());
        // Remove leading, trailing, and deduplicate repeated ascii spaces
        absl::RemoveExtraAsciiWhitespace(&value);
        const auto iter = map->find(std::string(entry.key().getStringView()));
        // If the entry already exists, append the new value to the end
        if (iter != map->end()) {
          iter->second += fmt::format(",{}", value);
        } else {
          map->emplace(std::string(entry.key().getStringView()), value);
        }
        return Http::HeaderMap::Iterate::Continue;
      },
      &out);
  // The AWS SDK has a quirk where it removes "default ports" (80, 443) from the host headers
  // Additionally, we canonicalize the :authority header as "host"
  // TODO(lavignes): This may need to be tweaked to canonicalize :authority for HTTP/2 requests
  const auto* authority_header = headers.Host();
  if (authority_header != nullptr && !authority_header->value().empty()) {
    const auto& value = authority_header->value().getStringView();
    const auto parts = StringUtil::splitToken(value, ":");
    if (parts.size() > 1 && (parts[1] == "80" || parts[1] == "443")) {
      // Has default port, so use only the host part
      out.emplace(Http::Headers::get().HostLegacy.get(), std::string(parts[0]));
    } else {
      out.emplace(Http::Headers::get().HostLegacy.get(), std::string(value));
    }
  }
  return out;
}

std::string
Utility::createCanonicalRequest(absl::string_view method, absl::string_view path,
                                const std::map<std::string, std::string>& canonical_headers,
                                absl::string_view content_hash) {
  std::vector<absl::string_view> parts;
  parts.emplace_back(method);
  // don't include the query part of the path
  const auto path_part = StringUtil::cropRight(path, "?");
  parts.emplace_back(path_part.empty() ? "/" : path_part);
  const auto query_part = StringUtil::cropLeft(path, "?");
  // if query_part == path_part, then there is no query
  parts.emplace_back(query_part == path_part ? "" : query_part);
  std::vector<std::string> formatted_headers;
  formatted_headers.reserve(canonical_headers.size());
  for (const auto& header : canonical_headers) {
    formatted_headers.emplace_back(fmt::format("{}:{}", header.first, header.second));
    parts.emplace_back(formatted_headers.back());
  }
  // need an extra blank space after the canonical headers
  parts.emplace_back("");
  const auto signed_headers = Utility::joinCanonicalHeaderNames(canonical_headers);
  parts.emplace_back(signed_headers);
  parts.emplace_back(content_hash);
  return absl::StrJoin(parts, "\n");
}

std::string
Utility::joinCanonicalHeaderNames(const std::map<std::string, std::string>& canonical_headers) {
  return absl::StrJoin(canonical_headers, ";", [](auto* out, const auto& pair) {
    return absl::StrAppend(out, pair.first);
  });
}

} // namespace Aws
} // namespace Common
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
