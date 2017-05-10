#include "common/router/config_utility.h"

#include <regex>
#include <string>
#include <vector>

#include "common/common/assert.h"

namespace Lyft {
namespace Router {

Upstream::ResourcePriority ConfigUtility::parsePriority(const Json::Object& config) {
  std::string priority_string = config.getString("priority", "default");
  if (priority_string == "default") {
    return Upstream::ResourcePriority::Default;
  } else {
    ASSERT(priority_string == "high");
    return Upstream::ResourcePriority::High;
  }
}

bool ConfigUtility::matchHeaders(const Http::HeaderMap& request_headers,
                                 const std::vector<HeaderData>& config_headers) {
  bool matches = true;

  if (!config_headers.empty()) {
    for (const HeaderData& cfg_header_data : config_headers) {
      const Http::HeaderEntry* header = request_headers.get(cfg_header_data.name_);
      if (cfg_header_data.value_.empty()) {
        matches &= (header != nullptr);
      } else if (!cfg_header_data.is_regex_) {
        matches &= (header != nullptr) && (header->value() == cfg_header_data.value_.c_str());
      } else {
        matches &= (header != nullptr) &&
                   std::regex_match(header->value().c_str(), cfg_header_data.regex_pattern_);
      }
      if (!matches) {
        break;
      }
    }
  }

  return matches;
}

} // Router
} // Lyft