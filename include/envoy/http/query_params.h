#pragma once

#include <map>
#include <string>

#include "absl/strings/string_view.h"
#include "envoy/common/pure.h"

namespace Envoy {
namespace Http {
namespace Utility {

// TODO(jmarantz): this should probably be a proper class, with methods to serialize
// using proper formatting. Perhaps similar to
// https://github.com/apache/incubator-pagespeed-mod/blob/master/pagespeed/kernel/http/query_params.h

typedef std::map<std::string, std::string> QueryParamsMap;

class QueryParams {
public:
  // QueryParams(absl::string_view url);
  virtual ~QueryParams();

  virtual QueryParamsMap parseQueryString(absl::string_view) PURE;
  virtual absl::string_view queryParamsToString(const QueryParamsMap&) PURE;
  virtual QueryParamsMap getQueryParams() PURE;
  virtual absl::string_view getURL() PURE;
  virtual const QueryParamsMap::const_iterator find(const std::string key) const PURE;
  virtual const QueryParamsMap::const_iterator end() const PURE;
};

/*
Utility::parseQueryString will call constructor to this class (?)

Probably need to rewrite queryParamsToString in here
  {{}} -> ""
  {{"a", "1"}} -> "?a=1"
  {{"a", "1"}, {"b", "2"}} -> "?a=1&b=2"
*/

} // namespace Utility
} // namespace Http
} // namespace Envoy
