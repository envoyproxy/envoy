#include "extensions/filters/listener/original_src/config.h"

namespace Envoy {
namespace Extensions {
namespace ListenerFilters {
namespace OriginalSrc {

Config::Config(const envoy::config::filter::listener::original_src::v2alpha1::OriginalSrc& config)
    : use_port_(config.bind_port()), mark_(config.mark()) {}

} // namespace OriginalSrc
} // namespace ListenerFilters
} // namespace Extensions
} // namespace Envoy
