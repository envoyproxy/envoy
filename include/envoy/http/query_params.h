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
  virtual ~QueryParams() {};

  /**
   * @param url supplies the url to parse.
   * @return QueryParamsMap of the parsed parameters, if any.
   */
  virtual QueryParamsMap parseQueryString(absl::string_view) PURE;

  /**
   * @param QueryParamsMap of desired string.
   * @return QueryParamsMap serialized into a string.
   */
  virtual absl::string_view queryParamsToString(const QueryParamsMap&) PURE;
  
  virtual const QueryParamsMap::const_iterator find(const std::string key) const PURE;
  virtual const QueryParamsMap::const_iterator end() const PURE;
};

} // namespace Utility
} // namespace Http
} // namespace Envoy
