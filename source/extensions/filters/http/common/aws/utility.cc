#include "extensions/filters/http/common/aws/utility.h"

#include "common/common/fmt.h"
#include "common/common/utility.h"

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
        // Pseudo-headers should not be canonicalized
        if (entry.key().c_str()[0] == ':') {
          return Http::HeaderMap::Iterate::Continue;
        }
        std::string value(entry.value().getStringView());
        // Remove leading, trailing, and deduplicate repeated ascii spaces
        absl::RemoveExtraAsciiWhitespace(&value);
        const auto iter = map->find(entry.key().c_str());
        // If the entry already exists, append the new value to the end
        if (iter != map->end()) {
          iter->second += fmt::format(",{}", value);
        } else {
          map->emplace(entry.key().c_str(), value);
        }
        return Http::HeaderMap::Iterate::Continue;
      },
      &out);
  // The AWS SDK has a quirk where it removes "default ports" (80, 443) from the host headers
  // Additionally, we canonicalize the :authority header as "host"
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

} // namespace Aws
} // namespace Common
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy