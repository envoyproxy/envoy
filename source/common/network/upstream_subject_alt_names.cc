#include "source/common/network/upstream_subject_alt_names.h"

#include "envoy/registry/registry.h"
#include "envoy/stream_info/filter_state.h"

#include "source/common/common/macros.h"

namespace Envoy {
namespace Network {

const std::string& UpstreamSubjectAltNames::key() {
  CONSTRUCT_ON_FIRST_USE(std::string, "envoy.network.upstream_subject_alt_names");
}

class UpstreamSubjectAltNamesObjectFactory : public StreamInfo::FilterState::ObjectFactory {
public:
  std::string name() const override { return UpstreamSubjectAltNames::key(); }
  std::unique_ptr<StreamInfo::FilterState::Object>
  createFromBytes(absl::string_view data) const override {
    const std::vector<std::string> parts = absl::StrSplit(data, ',');
    return std::make_unique<UpstreamSubjectAltNames>(parts);
  }
};

REGISTER_FACTORY(UpstreamSubjectAltNamesObjectFactory, StreamInfo::FilterState::ObjectFactory);
} // namespace Network
} // namespace Envoy
