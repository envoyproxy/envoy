#include <memory>
#include <vector>

#include "envoy/common/hashable.h"

#include "source/extensions/filters/common/set_filter_state/filter_config.h"

#include "test/mocks/server/server_factory_context.h"
#include "test/mocks/stream_info/mocks.h"
#include "test/test_common/utility.h"

#include "contrib/istio/filters/common/source/hashable_string.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace Istio {
namespace Common {
namespace {

TEST(HashableStringTest, TestHashableStringIsHashable) {
  using FilterStateValue =
      envoy::extensions::filters::common::set_filter_state::v3::FilterStateValue;
  using Config = Extensions::Filters::Common::SetFilterState::Config;

  NiceMock<Server::Configuration::MockGenericFactoryContext> context;
  Http::TestRequestHeaderMapImpl header_map;
  NiceMock<StreamInfo::MockStreamInfo> stream_info;

  FilterStateValue proto;
  TestUtility::loadFromYaml(R"YAML(
    object_key: key
    factory_key: istio.hashable_string
    format_string:
      text_format_source:
        inline_string: "value"
  )YAML",
                            proto);
  std::vector<FilterStateValue> protos{{proto}};

  auto config = std::make_shared<Config>(
      Protobuf::RepeatedPtrField<FilterStateValue>(protos.begin(), protos.end()),
      StreamInfo::FilterState::LifeSpan::FilterChain, context);
  config->updateFilterState({&header_map}, stream_info);

  const auto* s = stream_info.filterState()->getDataReadOnly<Hashable>("key");
  ASSERT_NE(s, nullptr);

  const HashableString* h = dynamic_cast<const HashableString*>(s);
  ASSERT_NE(h, nullptr);
  ASSERT_EQ(h->asString(), "value");
  ASSERT_EQ(h->serializeAsString(), "value");
}

} // namespace
} // namespace Common
} // namespace Istio
} // namespace Envoy
