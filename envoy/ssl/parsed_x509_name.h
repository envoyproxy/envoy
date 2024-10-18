#pragma once

#include <memory>
#include <string>
#include <vector>

#include "envoy/common/optref.h"

namespace Envoy {
namespace Ssl {

/**
 *  Attribute values parsed from a X.509 distinguished name. This only includes some
 *  well-known elements such as commonName (CN) and organizationName (O). The purpose is to
 *  avoid user parsing the RFC2253 string with their own code. The value will be UTF8 string,
 *  which means if the value type can not be converted to UTF8 string we'll just set empty
 *  string, to protect from malicious certificate.
 */
struct ParsedX509Name {
  // there should be only one commonName in the distinguished name
  std::string commonName_;
  // there could be multiple organizationNames
  std::vector<std::string> organizationName_;
  // TODO: add more well known fields such as L, OU, C, DC, UID etc.
};

using ParsedX509NameOptConstRef = OptRef<const ParsedX509Name>;
using ParsedX509NamePtr = std::unique_ptr<ParsedX509Name>;

} // namespace Ssl
} // namespace Envoy
