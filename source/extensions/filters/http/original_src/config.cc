#include "source/extensions/filters/http/original_src/config.h"

#include "envoy/extensions/filters/http/original_src/v3/original_src.pb.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace OriginalSrc {

Config::Config(const envoy::extensions::filters::http::original_src::v3::OriginalSrc& config)
    : mark_(config.mark()) {}

} // namespace OriginalSrc
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
