#include <memory>

#include "envoy/http/header_map.h"

#include "source/extensions/filters/http/thrift_to_metadata/filter.h"

#include "test/common/stream_info/test_util.h"
#include "test/mocks/http/mocks.h"
#include "test/mocks/server/mocks.h"
#include "test/mocks/stats/mocks.h"
#include "test/mocks/stream_info/mocks.h"
#include "test/test_common/utility.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace ThriftToMetadata {

class FilterTest : public testing::Test {
public:
  FilterTest() = default;

  const std::string config_yaml_ = R"EOF(
request_rules:
- field: PROTOCOL
  on_present:
    metadata_namespace: envoy.lb
    key: protocol
  on_missing:
    metadata_namespace: envoy.lb
    key: protocol
    value: "unknown"
- field: TRANSPORT
  on_present:
    metadata_namespace: envoy.lb
    key: transport
  on_missing:
    metadata_namespace: envoy.lb
    key: transport
    value: "unknown"
response_rules:
- field: MESSAGE_TYPE
  on_present:
    metadata_namespace: envoy.filters.http.thrift_to_metadata
    key: response_message_type
  on_missing:
    metadata_namespace: envoy.filters.http.thrift_to_metadata
    key: response_message_type
    value: "exception"
- field: REPLY_TYPE
  on_present:
    metadata_namespace: envoy.filters.http.thrift_to_metadata
    key: response_reply_type
  on_missing:
    metadata_namespace: envoy.filters.http.thrift_to_metadata
    key: response_reply_type
    value: "error"
)EOF";

  void initializeFilter(const std::string& yaml) {
    envoy::extensions::filters::http::thrift_to_metadata::v3::ThriftToMetadata config;
    TestUtility::loadFromYaml(yaml, config);
    config_ = std::make_shared<FilterConfig>(config, *scope_.rootScope());
    filter_ = std::make_shared<Filter>(config_);
    filter_->setDecoderFilterCallbacks(decoder_callbacks_);
    filter_->setEncoderFilterCallbacks(encoder_callbacks_);
  }

  NiceMock<Stats::MockIsolatedStatsStore> scope_;
  NiceMock<Http::MockStreamDecoderFilterCallbacks> decoder_callbacks_;
  NiceMock<Http::MockStreamEncoderFilterCallbacks> encoder_callbacks_;
  std::shared_ptr<FilterConfig> config_;
  std::shared_ptr<Filter> filter_;
};

TEST_F(FilterTest, Basic) {
  initializeFilter(config_yaml_);
  EXPECT_TRUE(true);
}

} // namespace ThriftToMetadata
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
