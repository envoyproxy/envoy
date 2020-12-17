#pragma once

#include <string>

#include "openssl/ssl.h"
#include "openssl/md5.h"

#include "absl/strings/ascii.h"

namespace Envoy {
namespace Extensions {
namespace Clusters {
namespace Redis {

class TwemHash {
public:
  /**
   * Twem ketama hash based on md5
   * see: https://github.com/twitter/twemproxy/blob/master/src/hashkit/nc_ketama.c#L31
   */
  static uint32_t hash(absl::string_view key, uint32_t alignment);

  static uint32_t fnv1a64(absl::string_view key);
};
} // namespace Redis
} // namespace Clusters
} // namespace Extensions
} // namespace Envoy
