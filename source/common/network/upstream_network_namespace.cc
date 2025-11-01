#include "source/common/network/upstream_network_namespace.h"

#include "envoy/registry/registry.h"
#include "envoy/stream_info/filter_state.h"

#include "source/common/common/macros.h"

namespace Envoy {
namespace Network {

const std::string& UpstreamNetworkNamespace::key() {
  CONSTRUCT_ON_FIRST_USE(std::string, "envoy.network.network_namespace");
}

class UpstreamNetworkNamespaceObjectFactory : public StreamInfo::FilterState::ObjectFactory {
public:
  std::string name() const override { return UpstreamNetworkNamespace::key(); }
  std::unique_ptr<StreamInfo::FilterState::Object>
  createFromBytes(absl::string_view data) const override {
    return std::make_unique<UpstreamNetworkNamespace>(data);
  }
};

REGISTER_FACTORY(UpstreamNetworkNamespaceObjectFactory, StreamInfo::FilterState::ObjectFactory);

} // namespace Network
} // namespace Envoy
