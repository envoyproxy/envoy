#include "source/extensions/filters/listener/original_src/config.h"

#include "envoy/extensions/filters/listener/original_src/v3/original_src.pb.h"

namespace Envoy {
namespace Extensions {
namespace ListenerFilters {
namespace OriginalSrc {

Config::Config(const envoy::extensions::filters::listener::original_src::v3::OriginalSrc& config)
    : use_port_(config.bind_port()), mark_(config.mark()) {}

} // namespace OriginalSrc
} // namespace ListenerFilters
} // namespace Extensions
} // namespace Envoy
