#include "source/common/network/application_protocol.h"

#include "envoy/registry/registry.h"
#include "envoy/stream_info/filter_state.h"

#include "source/common/common/macros.h"

namespace Envoy {
namespace Network {

const std::string& ApplicationProtocols::key() {
  CONSTRUCT_ON_FIRST_USE(std::string, "envoy.network.application_protocols");
}

class ApplicationProtocolsObjectFactory : public StreamInfo::FilterState::ObjectFactory {
public:
  std::string name() const override { return ApplicationProtocols::key(); }
  std::unique_ptr<StreamInfo::FilterState::Object>
  createFromBytes(absl::string_view data) const override {
    const std::vector<std::string> parts = absl::StrSplit(data, ',');
    return std::make_unique<ApplicationProtocols>(parts);
  }
};

REGISTER_FACTORY(ApplicationProtocolsObjectFactory, StreamInfo::FilterState::ObjectFactory);
} // namespace Network
} // namespace Envoy
