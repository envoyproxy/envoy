#include "source/common/network/upstream_subject_alt_names.h"

#include "source/common/common/macros.h"

namespace Envoy {
namespace Network {

const std::string& UpstreamSubjectAltNames::key() {
  CONSTRUCT_ON_FIRST_USE(std::string, "envoy.network.upstream_subject_alt_names");
}
} // namespace Network
} // namespace Envoy
