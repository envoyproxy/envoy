#include "contrib/sgx/private_key_providers/source/utility.h"

namespace Envoy {
namespace Extensions {
namespace PrivateKeyMethodProvider {
namespace Sgx {

bool isValidString(std::string s) {
  for (auto c : s) {
    if (!isalnum(c) && c != '_' && c != '-' && c != '/' && c != '=') {
      return false;
    }
  }
  return true;
}

} // namespace Sgx
} // namespace PrivateKeyMethodProvider
} // namespace Extensions
} // namespace Envoy
