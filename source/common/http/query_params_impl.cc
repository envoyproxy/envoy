#include "common/http/query_params_impl.h"
#include "common/common/utility.h"

#include "absl/strings/string_view.h"
#include "absl/strings/str_cat.h"


namespace Envoy {
namespace Http {
namespace Utility {

QueryParamsImpl::QueryParamsImpl() {}

QueryParamsMap QueryParamsImpl::parseQueryString(absl::string_view url) {
  QueryParamsMap params;
  size_t start = url.find('?');
  if (start == std::string::npos) {
    return params;
  }

  start++;
  while (start < url.size()) {
    size_t end = url.find('&', start);
    if (end == std::string::npos) {
      end = url.size();
    }
    absl::string_view param(url.data() + start, end - start);

    const size_t equal = param.find('=');
    if (equal != std::string::npos) {
      params.emplace(StringUtil::subspan(url, start, start + equal),
                            StringUtil::subspan(url, start + equal + 1, end));
    } else {
      params.emplace(StringUtil::subspan(url, start, end), "");
    }

    start = end + 1;
  }

  return params;
}

absl::string_view QueryParamsImpl::queryParamsToString(const QueryParamsMap& params) {
  std::string out;
  std::string delim = "?";
  for (auto p : params) {
    absl::StrAppend(&out, delim, p.first, "=", p.second);
    delim = "&";
  }
  return out;
}

const QueryParamsMap::const_iterator QueryParamsImpl::find(const std::string key) const { return this->find(key); }
const QueryParamsMap::const_iterator QueryParamsImpl::end() const { return this->end(); }

} // namespace Utility
} // namespace Http
} // namespace Envoy
