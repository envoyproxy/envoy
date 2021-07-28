#include "source/extensions/filters/http/sxg/sxg_utils.h"

#include "libsxg.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace SXG {

bool writeSxg(const sxg_signer_list_t* signers, const std::string url,
              const sxg_encoded_response_t* encoded, sxg_buffer_t* result) {
  return sxg_generate(url.c_str(), signers, encoded, result);
}

} // namespace SXG
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
