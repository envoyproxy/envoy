#pragma once

#include <map>
#include <string>

namespace Envoy {
namespace Http {
namespace Utility {

// TODO(jmarantz): this should probably be a proper class, with methods to serialize
// using proper formatting. Perhaps similar to
// https://github.com/apache/incubator-pagespeed-mod/blob/master/pagespeed/kernel/http/query_params.h

using QueryParams = std::map<std::string, std::string>;

} // namespace Utility
} // namespace Http
} // namespace Envoy
