#pragma once

#include "envoy/http/query_params.h"

namespace Envoy {
namespace Http {
namespace Utility {

class QueryParamsImpl : QueryParams {
public:
  QueryParamsImpl();
  ~QueryParamsImpl(){};

  /**
   * Parse a URL into query parameters.
   * @param url supplies the url to parse.
   * @return QueryParamsMap of the parsed parameters, if any.
   */
  QueryParamsMap parseQueryString(absl::string_view) override;

  /**
   * Serialize query-params into a string.
   */
  absl::string_view queryParamsToString(const QueryParamsMap&) override;

  /**
   * Applies std::map::find()
   */
  QueryParamsMap::const_iterator find(const std::string key) const override;

  /**
   * Applies std::map::begin()
   */
  QueryParamsMap::const_iterator begin() const override;
  /**
   * Applies std::map::end()
   */
  QueryParamsMap::const_iterator end() const override;

  /**
   * Applies std::map::size()
   */
  int size() override;
};

} // namespace Utility
} // namespace Http
} // namespace Envoy
