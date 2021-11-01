#include "source/common/network/matching/inputs.h"

#include "envoy/registry/registry.h"

namespace Envoy {
namespace Network {
namespace Matching {
REGISTER_FACTORY(SourceIpDataInputFactory, Matcher::DataInputFactory<NetworkMatchingData>);
} // namespace Matching
} // namespace Network
} // namespace Envoy
