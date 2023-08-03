#pragma once

#include <map>
#include <string>
#include <vector>

#include "absl/strings/string_view.h"
#include "header_map.h"

namespace Envoy {
namespace Http {
namespace Utility {

// TODO(jmarantz): this should probably be a proper class, with methods to serialize
// using proper formatting. Perhaps similar to
// https://github.com/apache/incubator-pagespeed-mod/blob/master/pagespeed/kernel/http/query_params.h

using QueryParams = std::map<std::string, std::string>;
using QueryParamsVector = std::vector<std::pair<std::string, std::string>>;

class QueryParamsV2 {
private:
  std::map<std::string, std::vector<std::string>> data;

public:
  void remove(std::string key);
  void add(std::string key, std::string value);
  void overwrite(std::string key, std::string value);
  std::string toString();
  std::string replaceQueryString(const HeaderString& path);

  static QueryParamsV2 parseParameters(absl::string_view data, size_t start, bool decode_params);
  static QueryParamsV2 parseQueryString(absl::string_view url);
  static QueryParamsV2 parseAndDecodeQueryString(absl::string_view url);
};

} // namespace Utility
} // namespace Http
} // namespace Envoy
