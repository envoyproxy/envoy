#include "common/ssl/isfips.h"

#include "openssl/ssl.h"

namespace Envoy {
namespace Ssl {

bool isFIPS() { return FIPS_mode(); }

} // namespace Ssl
} // namespace Envoy
