#pragma once

#include <map>
#include <string>
#include <vector>

#include "envoy/http/header_map.h"

#include "absl/container/btree_map.h"
#include "absl/strings/string_view.h"
#include "absl/types/optional.h"

namespace Envoy {
namespace Http {
namespace Utility {

using QueryParamsVector = std::vector<std::pair<std::string, std::string>>;

class QueryParamsMulti {
private:
  absl::btree_map<std::string, std::vector<std::string>> data_;

public:
  void remove(absl::string_view key);
  void add(absl::string_view key, absl::string_view value);
  void overwrite(absl::string_view key, absl::string_view value);
  std::string toString() const;
  std::string replaceQueryString(const HeaderString& path) const;
  absl::optional<std::string> getFirstValue(absl::string_view key) const;

  const absl::btree_map<std::string, std::vector<std::string>>& data() const { return data_; }

  static QueryParamsMulti parseParameters(absl::string_view data, size_t start, bool decode_params);
  static QueryParamsMulti parseQueryString(absl::string_view url);
  static QueryParamsMulti parseAndDecodeQueryString(absl::string_view url);
};

} // namespace Utility
} // namespace Http
} // namespace Envoy
