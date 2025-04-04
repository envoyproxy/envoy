#include "envoy/config/bootstrap/v3/bootstrap.pb.h"
#include "envoy/config/listener/v3/listener_components.pb.h"
#include "envoy/extensions/common/matching/v3/extension_matcher.pb.validate.h"

#include "source/extensions/filters/network/match_delegate/config.h"

#include "test/integration/integration.h"
#include "test/integration/utility.h"
#include "test/mocks/server/options.h"
#include "test/test_common/utility.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace MatchDelegate {
namespace {

using envoy::extensions::common::matching::v3::ExtensionWithMatcher;
using Envoy::Protobuf::MapPair;
using Envoy::ProtobufWkt::Any;

} // namespace
} // namespace MatchDelegate
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
