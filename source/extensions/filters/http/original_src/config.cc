#include "extensions/filters/http/original_src/config.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace OriginalSrc {

Config::Config(const envoy::config::filter::http::original_src::v2alpha1::OriginalSrc& config)
    : mark_(config.mark()) {}

} // namespace OriginalSrc
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
