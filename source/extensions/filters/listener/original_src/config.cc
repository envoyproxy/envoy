#include "extensions/filters/network/original_src/config.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace OriginalSrc {

Config::Config(const envoy::config::filter::network::original_src::v2alpha1::OriginalSrc& config)
    : use_port_(config.bind_port()), mark_(config.mark()) {}

} // namespace OriginalSrc
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
