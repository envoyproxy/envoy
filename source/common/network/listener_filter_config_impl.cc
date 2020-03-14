#include "common/network/listener_filter_config_impl.h"

namespace Envoy {
namespace Network {

ListenerFilterConfigImpl::ListenerFilterConfigImpl(
    const Network::ListenerFilterMatcherSharedPtr& listener_filter_matcher)
    : matcher_(listener_filter_matcher) {}
ListenerFilterMatcherSharedPtr ListenerFilterConfigImpl::matcher() const { return nullptr; }
} // namespace Network
} // namespace Envoy