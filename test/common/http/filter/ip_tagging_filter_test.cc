#include "common/buffer/buffer_impl.h"
#include "common/http/filter/ip_tagging_filter.h"
#include "common/http/header_map_impl.h"
#include "common/http/headers.h"

#include "test/mocks/http/mocks.h"
#include "test/test_common/utility.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::Return;
using testing::_;

namespace Envoy {
namespace Http {

class IpTaggingFilterTest : public testing::Test {
public:
  const std::string internal_request_json = R"EOF(
    {
      "request_type" : "internal",
      "ip_tags" : [
        {
          "ip_tag_name" : "test_internal",
          "ip_list" : ["1.2.3.4"]
        }
      ]
    }
  )EOF";

  const std::string external_request_json = R"EOF(
    {
      "request_type" : "external",
      "ip_tags" : [
        {
          "ip_tag_name" : "test_external",
          "ip_list" : ["1.2.3.4"]
        }
      ]
    }
  )EOF";

  const std::string both_request_json = R"EOF(
    {
      "request_type" : "both",
      "ip_tags" : [
        {
          "ip_tag_name" : "test_both",
          "ip_list" : ["1.2.3.4"]
        }
      ]
    }
  )EOF";

  void SetUpTest(const std::string json) {
    Json::ObjectSharedPtr config = Json::Factory::loadFromString(json);
    config_.reset(new IpTaggingFilterConfig(*config));
    filter_.reset(new IpTaggingFilter(config_));
    filter_->setDecoderFilterCallbacks(filter_callbacks_);
  }

  ~IpTaggingFilterTest() { filter_->onDestroy(); }

  IpTaggingFilterConfigSharedPtr config_;
  std::unique_ptr<IpTaggingFilter> filter_;
  NiceMock<MockStreamDecoderFilterCallbacks> filter_callbacks_;
  TestHeaderMapImpl request_headers_;
  Buffer::OwnedImpl data_;
};

TEST_F(IpTaggingFilterTest, InternalRequest) {
  SetUpTest(internal_request_json);

  EXPECT_EQ(FilterHeadersStatus::Continue, filter_->decodeHeaders(request_headers_, false));
  EXPECT_EQ(FilterDataStatus::Continue, filter_->decodeData(data_, false));
  EXPECT_EQ(FilterTrailersStatus::Continue, filter_->decodeTrailers(request_headers_));
}

TEST_F(IpTaggingFilterTest, ExternalRequest) {
  SetUpTest(external_request_json);

  EXPECT_EQ(FilterHeadersStatus::Continue, filter_->decodeHeaders(request_headers_, false));
  EXPECT_EQ(FilterDataStatus::Continue, filter_->decodeData(data_, false));
  EXPECT_EQ(FilterTrailersStatus::Continue, filter_->decodeTrailers(request_headers_));
}

TEST_F(IpTaggingFilterTest, BothRequest) {
  SetUpTest(both_request_json);

  EXPECT_EQ(FilterHeadersStatus::Continue, filter_->decodeHeaders(request_headers_, false));
  EXPECT_EQ(FilterDataStatus::Continue, filter_->decodeData(data_, false));
  EXPECT_EQ(FilterTrailersStatus::Continue, filter_->decodeTrailers(request_headers_));
}

} // namespace Http
} // namespace Envoy
