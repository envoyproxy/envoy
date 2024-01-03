#include "source/common/network/upstream_server_name.h"

#include "envoy/registry/registry.h"
#include "envoy/stream_info/filter_state.h"

#include "source/common/common/macros.h"

namespace Envoy {
namespace Network {

const std::string& UpstreamServerName::key() {
  CONSTRUCT_ON_FIRST_USE(std::string, "envoy.network.upstream_server_name");
}

class UpstreamServerNameObjectFactory : public StreamInfo::FilterState::ObjectFactory {
public:
  std::string name() const override { return UpstreamServerName::key(); }
  std::unique_ptr<StreamInfo::FilterState::Object>
  createFromBytes(absl::string_view data) const override {
    return std::make_unique<UpstreamServerName>(data);
  }
};

REGISTER_FACTORY(UpstreamServerNameObjectFactory, StreamInfo::FilterState::ObjectFactory);

} // namespace Network
} // namespace Envoy
