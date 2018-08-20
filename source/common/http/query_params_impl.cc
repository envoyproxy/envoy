#include "common/http/query_params_impl.h"
#include "common/common/utility.h"

#include "absl/strings/string_view.h"
#include "absl/strings/str_cat.h"


namespace Envoy {
namespace Http {
namespace Utility {

QueryParamsMap QueryParamsImpl::parseQueryString(absl::string_view url) {
  size_t start = url.find('?');
  if (start == std::string::npos) {
    return query_params_;
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
      query_params_.emplace(StringUtil::subspan(url, start, start + equal),
                            StringUtil::subspan(url, start + equal + 1, end));
    } else {
      query_params_.emplace(StringUtil::subspan(url, start, end), "");
    }

    start = end + 1;
  }

  return query_params_;
}

QueryParamsImpl::QueryParamsImpl() {}
QueryParamsImpl::QueryParamsImpl(absl::string_view url) : url_(url) { parseQueryString(url); }

QueryParamsMap QueryParamsImpl::getQueryParams() { return query_params_; }

absl::string_view QueryParamsImpl::queryParamsToString(const QueryParamsMap& params) {
  std::string out;
  std::string delim = "?";
  for (auto p : params) {
    absl::StrAppend(&out, delim, p.first, "=", p.second);
    delim = "&";
  }
  return out;
}

absl::string_view QueryParamsImpl::getURL() { return url_; }

const QueryParamsMap::const_iterator QueryParamsImpl::find(const std::string key) const { return query_params_.find(key); }
const QueryParamsMap::const_iterator QueryParamsImpl::end() const { return query_params_.end(); }

} // namespace Utility
} // namespace Http
} // namespace Envoy
