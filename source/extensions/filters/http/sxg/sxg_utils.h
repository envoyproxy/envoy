#pragma once

#include "libsxg.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace SXG {

bool writeSxg(const sxg_signer_list_t*, const std::string, const sxg_encoded_response_t*,
              sxg_buffer_t*);

} // namespace SXG
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
