#include "source/extensions/filters/network/meta_protocol_proxy/proxy.h"

#include "test/extensions/filters/network/meta_protocol_proxy/fake_codec.h"

#include "test/extensions/filters/network/meta_protocol_proxy/mocks/filter.h"
#include "test/extensions/filters/network/meta_protocol_proxy/mocks/route.h"
#include "test/extensions/filters/network/meta_protocol_proxy/mocks/codec.h"
#include "test/test_common/utility.h"

#include "gtest/gtest.h"
#include <memory>

using testing::NiceMock;

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace MetaProtocolProxy {
namespace {

class FilterConfigMockWrapper {
public:
  std::shared_ptr<FilterConfig> filter_config_;

  std::string stat_prefix_;

  NiceMock<MockCodecFactory>* codec_factory_;

  NiceMock<MockRouteMatcher>* route_matcher_;

  std::shared_ptr<MockStreamFilter> mock_stream_filter_;
};

TEST(ProxyFilterTest, EmptyTestForBuild) {}

} // namespace
} // namespace MetaProtocolProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
