#include "common/protobuf/utility.h"

#include "extensions/filters/http/cache/cache_filter.h"

#include "test/mocks/http/mocks.h"
#include "test/mocks/runtime/mocks.h"
#include "test/mocks/stats/mocks.h"
#include "test/test_common/simulated_time_system.h"
#include "test/test_common/utility.h"

#include "gtest/gtest.h"

using testing::Return;

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace Cache {

class CacheFilterTest : public testing::Test {
protected:
  CacheFilterTest() {
    ON_CALL(runtime_.snapshot_, featureEnabled("cache.filter_enabled", 100))
        .WillByDefault(Return(true));
  }

  void SetUp() override { setUpFilter(R"EOF({"name": "SimpleHttpCache"})EOF"); }

  // CacheFilterTest Helpers
  void setUpFilter(std::string&& json) {
    Json::ObjectSharedPtr config = Json::Factory::loadFromString(json);
    envoy::config::filter::http::cache::v2alpha::Cache cache_options;
    MessageUtil::loadFromJson(json, cache_options);
    config_ =
        std::make_shared<CacheFilterConfig>(cache_options, "test.", stats_, runtime_, time_system_);
    filter_ = std::make_unique<CacheFilter>(config_);
  }

  void feedBuffer(uint64_t size) {
    TestUtility::feedBufferWithRandomCharacters(data_, size);
    expected_str_ += data_.toString();
  }

  void doRequest(Http::TestHeaderMapImpl&& headers, bool end_stream) {
    EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->decodeHeaders(headers, end_stream));
  }

  Event::SimulatedTimeSystem time_system_;
  CacheFilterConfigSharedPtr config_;
  std::unique_ptr<CacheFilter> filter_;
  Buffer::OwnedImpl data_;
  std::string expected_str_;
  Stats::IsolatedStoreImpl stats_;
  NiceMock<Runtime::MockLoader> runtime_;
};

TEST_F(CacheFilterTest, basic) {}

} // namespace Cache
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
