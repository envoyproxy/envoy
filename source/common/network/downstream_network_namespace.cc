#include "source/common/network/downstream_network_namespace.h"

#include "envoy/registry/registry.h"
#include "envoy/stream_info/filter_state.h"

#include "source/common/common/macros.h"

namespace Envoy {
namespace Network {

const std::string& DownstreamNetworkNamespace::key() {
  CONSTRUCT_ON_FIRST_USE(std::string, "envoy.network.network_namespace");
}

class DownstreamNetworkNamespaceObjectFactory : public StreamInfo::FilterState::ObjectFactory {
public:
  std::string name() const override { return DownstreamNetworkNamespace::key(); }
  std::unique_ptr<StreamInfo::FilterState::Object>
  createFromBytes(absl::string_view data) const override {
    return std::make_unique<DownstreamNetworkNamespace>(data);
  }
};

REGISTER_FACTORY(DownstreamNetworkNamespaceObjectFactory, StreamInfo::FilterState::ObjectFactory);

} // namespace Network
} // namespace Envoy
