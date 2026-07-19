#include <memory>

#include "envoy/extensions/filters/http/priority_load_shed/v3/priority_load_shed.pb.h"
#include "envoy/http/codes.h"
#include "envoy/stream_info/stream_info.h"

#include "source/common/http/headers.h"
#include "source/common/stream_info/utility.h"
#include "source/extensions/filters/http/priority_load_shed/filter.h"

#include "test/common/stats/stat_test_utility.h"
#include "test/mocks/http/mocks.h"
#include "test/mocks/server/overload_manager.h"
#include "test/test_common/utility.h"

#include "absl/strings/str_cat.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::_;
using testing::HasSubstr;
using testing::Return;

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace PriorityLoadShed {
namespace {

using ProtoConfig = envoy::extensions::filters::http::priority_load_shed::v3::PriorityLoadShed;

class PriorityLoadShedFilterTest : public testing::Test {
public:
  PriorityLoadShedFilterTest() {
    ON_CALL(overload_manager_, getLoadShedPoint(_)).WillByDefault(Return(nullptr));
  }

  void initializeFilter(const std::string& yaml) {
    ProtoConfig config;
    TestUtility::loadFromYaml(yaml, config);
    auto config_or = PriorityLoadShedFilterConfig::create(config, overload_manager_, "prefix.",
                                                          *stats_store_.rootScope());
    ASSERT_TRUE(config_or.ok()) << config_or.status();
    config_ = std::move(config_or.value());
    filter_ = std::make_unique<PriorityLoadShedFilter>(config_);
    filter_->setDecoderFilterCallbacks(decoder_callbacks_);
  }

  Http::TestRequestHeaderMapImpl defaultHeaders() {
    return {{":method", "GET"}, {":path", "/"}, {":scheme", "http"}, {":authority", "example.com"}};
  }

  Stats::TestUtil::TestStore stats_store_;
  NiceMock<Server::MockOverloadManager> overload_manager_;
  NiceMock<Server::MockLoadShedPoint> low_priority_bucket_;
  NiceMock<Server::MockLoadShedPoint> high_priority_bucket_;
  NiceMock<Server::MockLoadShedPoint> default_bucket_;
  NiceMock<Http::MockStreamDecoderFilterCallbacks> decoder_callbacks_;
  std::shared_ptr<PriorityLoadShedFilterConfig> config_;
  std::unique_ptr<PriorityLoadShedFilter> filter_;
};

TEST_F(PriorityLoadShedFilterTest, RejectsWithOverloadWhenMatchingBucketSheds) {
  EXPECT_CALL(overload_manager_, getLoadShedPoint("envoy.load_shed_points.priority.high"))
      .WillOnce(Return(&high_priority_bucket_));
  EXPECT_CALL(overload_manager_, getLoadShedPoint("envoy.load_shed_points.priority.low"))
      .WillOnce(Return(&low_priority_bucket_));
  initializeFilter(R"EOF(
header_name: "x-message-priority"
buckets:
- value_range: { start: 0, end: 16 }
  load_shed_point: "envoy.load_shed_points.priority.high"
- value_range: { start: 16, end: 32 }
  load_shed_point: "envoy.load_shed_points.priority.low"
)EOF");

  EXPECT_CALL(low_priority_bucket_, shouldShedLoad()).WillOnce(Return(true));
  EXPECT_CALL(decoder_callbacks_.stream_info_,
              setResponseFlag(StreamInfo::CoreResponseFlag::OverloadManager));
  EXPECT_CALL(decoder_callbacks_, sendLocalReply(Http::Code::ServiceUnavailable, "envoy overloaded",
                                                 _, std::optional<Grpc::Status::GrpcStatus>(),
                                                 StreamInfo::ResponseCodeDetails::get().Overload));

  auto headers = defaultHeaders();
  headers.addCopy("x-message-priority", "20");
  EXPECT_EQ(Http::FilterHeadersStatus::StopIteration, filter_->decodeHeaders(headers, true));
  EXPECT_EQ(1UL, stats_store_.counter("prefix.priority_load_shed.shed").value());
}

TEST_F(PriorityLoadShedFilterTest, MissingHeaderRejectsWithoutDefault) {
  EXPECT_CALL(overload_manager_, getLoadShedPoint("envoy.load_shed_points.priority.low"))
      .WillOnce(Return(&low_priority_bucket_));
  initializeFilter(R"EOF(
header_name: "x-message-priority"
buckets:
- value_range: { start: 16, end: 32 }
  load_shed_point: "envoy.load_shed_points.priority.low"
)EOF");

  EXPECT_CALL(low_priority_bucket_, shouldShedLoad()).Times(0);
  EXPECT_CALL(decoder_callbacks_, sendLocalReply(Http::Code::BadRequest, "missing priority header",
                                                 _, std::optional<Grpc::Status::GrpcStatus>(),
                                                 "priority_load_shed.missing_header"));
  auto headers = defaultHeaders();
  EXPECT_EQ(Http::FilterHeadersStatus::StopIteration, filter_->decodeHeaders(headers, true));
  EXPECT_EQ(1UL, stats_store_.counter("prefix.priority_load_shed.header_missing").value());
}

TEST_F(PriorityLoadShedFilterTest, MissingHeaderShedsWithDefaultLoadShedPoint) {
  EXPECT_CALL(overload_manager_, getLoadShedPoint("envoy.load_shed_points.priority.low"))
      .WillOnce(Return(&low_priority_bucket_));
  EXPECT_CALL(overload_manager_, getLoadShedPoint("envoy.load_shed_points.priority.default"))
      .WillOnce(Return(&default_bucket_));
  initializeFilter(R"EOF(
header_name: "x-message-priority"
buckets:
- value_range: { start: 16, end: 32 }
  load_shed_point: "envoy.load_shed_points.priority.low"
default_load_shed_point: "envoy.load_shed_points.priority.default"
)EOF");

  EXPECT_CALL(default_bucket_, shouldShedLoad()).WillOnce(Return(true));
  EXPECT_CALL(decoder_callbacks_.stream_info_,
              setResponseFlag(StreamInfo::CoreResponseFlag::OverloadManager));
  EXPECT_CALL(decoder_callbacks_, sendLocalReply(Http::Code::ServiceUnavailable, "envoy overloaded",
                                                 _, std::optional<Grpc::Status::GrpcStatus>(),
                                                 StreamInfo::ResponseCodeDetails::get().Overload));

  auto headers = defaultHeaders();
  EXPECT_EQ(Http::FilterHeadersStatus::StopIteration, filter_->decodeHeaders(headers, true));
  EXPECT_EQ(1UL, stats_store_.counter("prefix.priority_load_shed.header_missing").value());
  EXPECT_EQ(1UL, stats_store_.counter("prefix.priority_load_shed.shed").value());
}

TEST_F(PriorityLoadShedFilterTest, InvalidHeaderRejectsWithoutDefault) {
  EXPECT_CALL(overload_manager_, getLoadShedPoint("envoy.load_shed_points.priority.low"))
      .WillOnce(Return(&low_priority_bucket_));
  initializeFilter(R"EOF(
header_name: "x-message-priority"
buckets:
- value_range: { start: 16, end: 32 }
  load_shed_point: "envoy.load_shed_points.priority.low"
)EOF");

  EXPECT_CALL(low_priority_bucket_, shouldShedLoad()).Times(0);
  EXPECT_CALL(decoder_callbacks_, sendLocalReply(Http::Code::BadRequest, "invalid priority header",
                                                 _, std::optional<Grpc::Status::GrpcStatus>(),
                                                 "priority_load_shed.invalid_header"));
  auto headers = defaultHeaders();
  headers.addCopy("x-message-priority", "not-an-integer");
  EXPECT_EQ(Http::FilterHeadersStatus::StopIteration, filter_->decodeHeaders(headers, true));
  EXPECT_EQ(1UL, stats_store_.counter("prefix.priority_load_shed.header_invalid").value());
}

TEST_F(PriorityLoadShedFilterTest, InvalidHeaderPassesWithDefaultLoadShedPoint) {
  EXPECT_CALL(overload_manager_, getLoadShedPoint("envoy.load_shed_points.priority.low"))
      .WillOnce(Return(&low_priority_bucket_));
  EXPECT_CALL(overload_manager_, getLoadShedPoint("envoy.load_shed_points.priority.default"))
      .WillOnce(Return(&default_bucket_));
  initializeFilter(R"EOF(
header_name: "x-message-priority"
buckets:
- value_range: { start: 16, end: 32 }
  load_shed_point: "envoy.load_shed_points.priority.low"
default_load_shed_point: "envoy.load_shed_points.priority.default"
)EOF");

  EXPECT_CALL(default_bucket_, shouldShedLoad()).WillOnce(Return(false));
  auto headers = defaultHeaders();
  headers.addCopy("x-message-priority", "not-an-integer");
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->decodeHeaders(headers, true));
  EXPECT_EQ(1UL, stats_store_.counter("prefix.priority_load_shed.header_invalid").value());
  EXPECT_EQ(1UL, stats_store_.counter("prefix.priority_load_shed.passed").value());
}

TEST_F(PriorityLoadShedFilterTest, RejectsUnresolvedDefaultLoadShedPointOnCreate) {
  EXPECT_CALL(overload_manager_, getLoadShedPoint("envoy.load_shed_points.priority.low"))
      .WillOnce(Return(&low_priority_bucket_));
  EXPECT_CALL(overload_manager_, getLoadShedPoint("envoy.load_shed_points.priority.default"))
      .WillOnce(Return(nullptr));

  ProtoConfig config;
  TestUtility::loadFromYaml(R"EOF(
header_name: "x-message-priority"
buckets:
- value_range: { start: 16, end: 32 }
  load_shed_point: "envoy.load_shed_points.priority.low"
default_load_shed_point: "envoy.load_shed_points.priority.default"
)EOF",
                            config);

  auto config_or = PriorityLoadShedFilterConfig::create(config, overload_manager_, "prefix.",
                                                        *stats_store_.rootScope());
  EXPECT_FALSE(config_or.ok());
  EXPECT_THAT(absl::StrCat(config_or.status()),
              HasSubstr("default load shed point 'envoy.load_shed_points.priority.default' is not configured"));
}

TEST_F(PriorityLoadShedFilterTest, NegativeHeaderRejectsWithoutDefault) {
  EXPECT_CALL(overload_manager_, getLoadShedPoint("envoy.load_shed_points.priority.low"))
      .WillOnce(Return(&low_priority_bucket_));
  initializeFilter(R"EOF(
header_name: "x-message-priority"
buckets:
- value_range: { start: 16, end: 32 }
  load_shed_point: "envoy.load_shed_points.priority.low"
)EOF");

  EXPECT_CALL(low_priority_bucket_, shouldShedLoad()).Times(0);
  EXPECT_CALL(decoder_callbacks_, sendLocalReply(Http::Code::BadRequest, "invalid priority header",
                                                 _, std::optional<Grpc::Status::GrpcStatus>(),
                                                 "priority_load_shed.invalid_header"));
  auto headers = defaultHeaders();
  headers.addCopy("x-message-priority", "-1");
  EXPECT_EQ(Http::FilterHeadersStatus::StopIteration, filter_->decodeHeaders(headers, true));
  EXPECT_EQ(1UL, stats_store_.counter("prefix.priority_load_shed.header_invalid").value());
}

TEST_F(PriorityLoadShedFilterTest, FirstHeaderValueOnly) {
  EXPECT_CALL(overload_manager_, getLoadShedPoint("envoy.load_shed_points.priority.low"))
      .WillOnce(Return(&low_priority_bucket_));
  initializeFilter(R"EOF(
header_name: "x-message-priority"
buckets:
- value_range: { start: 16, end: 32 }
  load_shed_point: "envoy.load_shed_points.priority.low"
)EOF");

  EXPECT_CALL(low_priority_bucket_, shouldShedLoad()).Times(0);
  EXPECT_CALL(decoder_callbacks_, sendLocalReply(Http::Code::BadRequest, "invalid priority header",
                                                 _, std::optional<Grpc::Status::GrpcStatus>(),
                                                 "priority_load_shed.invalid_header"));
  auto headers = defaultHeaders();
  headers.addCopy("x-message-priority", "bad-first-value");
  headers.addCopy("x-message-priority", "20");
  EXPECT_EQ(Http::FilterHeadersStatus::StopIteration, filter_->decodeHeaders(headers, true));
  EXPECT_EQ(1UL, stats_store_.counter("prefix.priority_load_shed.header_invalid").value());
}

TEST_F(PriorityLoadShedFilterTest, BucketStartBoundaryIsIncluded) {
  EXPECT_CALL(overload_manager_, getLoadShedPoint("envoy.load_shed_points.priority.low"))
      .WillOnce(Return(&low_priority_bucket_));
  initializeFilter(R"EOF(
header_name: "x-message-priority"
buckets:
- value_range: { start: 16, end: 32 }
  load_shed_point: "envoy.load_shed_points.priority.low"
)EOF");

  EXPECT_CALL(low_priority_bucket_, shouldShedLoad()).WillOnce(Return(false));
  auto headers = defaultHeaders();
  headers.addCopy("x-message-priority", "16");
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->decodeHeaders(headers, true));
  EXPECT_EQ(1UL, stats_store_.counter("prefix.priority_load_shed.passed").value());
}

TEST_F(PriorityLoadShedFilterTest, BucketEndBoundaryIsExcluded) {
  EXPECT_CALL(overload_manager_, getLoadShedPoint("envoy.load_shed_points.priority.low"))
      .WillOnce(Return(&low_priority_bucket_));
  initializeFilter(R"EOF(
header_name: "x-message-priority"
buckets:
- value_range: { start: 16, end: 32 }
  load_shed_point: "envoy.load_shed_points.priority.low"
)EOF");

  EXPECT_CALL(low_priority_bucket_, shouldShedLoad()).Times(0);
  EXPECT_CALL(decoder_callbacks_,
              sendLocalReply(Http::Code::BadRequest,
                             "priority value does not match any configured bucket", _,
                             std::optional<Grpc::Status::GrpcStatus>(),
                             "priority_load_shed.bucket_unmatched"));
  auto headers = defaultHeaders();
  headers.addCopy("x-message-priority", "32");
  EXPECT_EQ(Http::FilterHeadersStatus::StopIteration, filter_->decodeHeaders(headers, true));
  EXPECT_EQ(1UL, stats_store_.counter("prefix.priority_load_shed.bucket_unmatched").value());
}

TEST_F(PriorityLoadShedFilterTest, UnmatchedValueRejectsWithoutDefault) {
  EXPECT_CALL(overload_manager_, getLoadShedPoint("envoy.load_shed_points.priority.low"))
      .WillOnce(Return(&low_priority_bucket_));
  initializeFilter(R"EOF(
header_name: "x-message-priority"
buckets:
- value_range: { start: 16, end: 32 }
  load_shed_point: "envoy.load_shed_points.priority.low"
)EOF");

  EXPECT_CALL(low_priority_bucket_, shouldShedLoad()).Times(0);
  EXPECT_CALL(decoder_callbacks_,
              sendLocalReply(Http::Code::BadRequest,
                             "priority value does not match any configured bucket", _,
                             std::optional<Grpc::Status::GrpcStatus>(),
                             "priority_load_shed.bucket_unmatched"));
  auto headers = defaultHeaders();
  headers.addCopy("x-message-priority", "5");
  EXPECT_EQ(Http::FilterHeadersStatus::StopIteration, filter_->decodeHeaders(headers, true));
  EXPECT_EQ(1UL, stats_store_.counter("prefix.priority_load_shed.bucket_unmatched").value());
}

TEST_F(PriorityLoadShedFilterTest, UnmatchedValueShedsWithDefaultLoadShedPoint) {
  EXPECT_CALL(overload_manager_, getLoadShedPoint("envoy.load_shed_points.priority.low"))
      .WillOnce(Return(&low_priority_bucket_));
  EXPECT_CALL(overload_manager_, getLoadShedPoint("envoy.load_shed_points.priority.default"))
      .WillOnce(Return(&default_bucket_));
  initializeFilter(R"EOF(
header_name: "x-message-priority"
buckets:
- value_range: { start: 16, end: 32 }
  load_shed_point: "envoy.load_shed_points.priority.low"
default_load_shed_point: "envoy.load_shed_points.priority.default"
)EOF");

  EXPECT_CALL(default_bucket_, shouldShedLoad()).WillOnce(Return(true));
  EXPECT_CALL(decoder_callbacks_.stream_info_,
              setResponseFlag(StreamInfo::CoreResponseFlag::OverloadManager));
  EXPECT_CALL(decoder_callbacks_, sendLocalReply(Http::Code::ServiceUnavailable, "envoy overloaded",
                                                 _, std::optional<Grpc::Status::GrpcStatus>(),
                                                 StreamInfo::ResponseCodeDetails::get().Overload));

  auto headers = defaultHeaders();
  headers.addCopy("x-message-priority", "5");
  EXPECT_EQ(Http::FilterHeadersStatus::StopIteration, filter_->decodeHeaders(headers, true));
  EXPECT_EQ(1UL, stats_store_.counter("prefix.priority_load_shed.bucket_unmatched").value());
  EXPECT_EQ(1UL, stats_store_.counter("prefix.priority_load_shed.shed").value());
}

TEST_F(PriorityLoadShedFilterTest, UnmatchedValuePassesWithDefaultLoadShedPoint) {
  EXPECT_CALL(overload_manager_, getLoadShedPoint("envoy.load_shed_points.priority.low"))
      .WillOnce(Return(&low_priority_bucket_));
  EXPECT_CALL(overload_manager_, getLoadShedPoint("envoy.load_shed_points.priority.default"))
      .WillOnce(Return(&default_bucket_));
  initializeFilter(R"EOF(
header_name: "x-message-priority"
buckets:
- value_range: { start: 16, end: 32 }
  load_shed_point: "envoy.load_shed_points.priority.low"
default_load_shed_point: "envoy.load_shed_points.priority.default"
)EOF");

  EXPECT_CALL(default_bucket_, shouldShedLoad()).WillOnce(Return(false));
  auto headers = defaultHeaders();
  headers.addCopy("x-message-priority", "5");
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->decodeHeaders(headers, true));
  EXPECT_EQ(1UL, stats_store_.counter("prefix.priority_load_shed.bucket_unmatched").value());
  EXPECT_EQ(1UL, stats_store_.counter("prefix.priority_load_shed.passed").value());
}

TEST_F(PriorityLoadShedFilterTest, RejectsUnresolvedBucketLoadShedPointOnCreate) {
  EXPECT_CALL(overload_manager_, getLoadShedPoint("envoy.load_shed_points.priority.low"))
      .WillOnce(Return(nullptr));

  ProtoConfig config;
  TestUtility::loadFromYaml(R"EOF(
header_name: "x-message-priority"
buckets:
- value_range: { start: 16, end: 32 }
  load_shed_point: "envoy.load_shed_points.priority.low"
)EOF",
                            config);

  auto config_or = PriorityLoadShedFilterConfig::create(config, overload_manager_, "prefix.",
                                                        *stats_store_.rootScope());
  EXPECT_FALSE(config_or.ok());
  EXPECT_THAT(absl::StrCat(config_or.status()),
              HasSubstr("load shed point 'envoy.load_shed_points.priority.low' is not configured"));
}

TEST_F(PriorityLoadShedFilterTest, PassingBucketContinues) {
  EXPECT_CALL(overload_manager_, getLoadShedPoint("envoy.load_shed_points.priority.low"))
      .WillOnce(Return(&low_priority_bucket_));
  initializeFilter(R"EOF(
header_name: "x-message-priority"
buckets:
- value_range: { start: 16, end: 32 }
  load_shed_point: "envoy.load_shed_points.priority.low"
)EOF");

  EXPECT_CALL(low_priority_bucket_, shouldShedLoad()).WillOnce(Return(false));
  auto headers = defaultHeaders();
  headers.addCopy("x-message-priority", "20");
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->decodeHeaders(headers, true));
  EXPECT_EQ(1UL, stats_store_.counter("prefix.priority_load_shed.passed").value());
}

TEST_F(PriorityLoadShedFilterTest, RejectsNegativeRangeStartOnCreate) {
  ProtoConfig config;
  config.set_header_name("x-message-priority");
  auto* bucket = config.add_buckets();
  bucket->mutable_value_range()->set_start(-1);
  bucket->mutable_value_range()->set_end(16);
  bucket->set_load_shed_point("envoy.load_shed_points.priority.high");

  auto config_or = PriorityLoadShedFilterConfig::create(config, overload_manager_, "prefix.",
                                                        *stats_store_.rootScope());
  EXPECT_FALSE(config_or.ok());
  EXPECT_THAT(absl::StrCat(config_or.status()), HasSubstr("invalid bucket range"));
}

TEST_F(PriorityLoadShedFilterTest, RejectsInvalidBucketRangeOnCreate) {
  ProtoConfig config;
  TestUtility::loadFromYaml(R"EOF(
header_name: "x-message-priority"
buckets:
- value_range: { start: 31, end: 31 }
  load_shed_point: "envoy.load_shed_points.priority.low"
)EOF",
                            config);

  auto config_or = PriorityLoadShedFilterConfig::create(config, overload_manager_, "prefix.",
                                                        *stats_store_.rootScope());
  EXPECT_FALSE(config_or.ok());
  EXPECT_THAT(absl::StrCat(config_or.status()), HasSubstr("invalid bucket range"));
}

TEST_F(PriorityLoadShedFilterTest, RejectsOverlappingRangesOnCreate) {
  ProtoConfig config;
  TestUtility::loadFromYaml(R"EOF(
header_name: "x-message-priority"
buckets:
- value_range: { start: 0, end: 16 }
  load_shed_point: "envoy.load_shed_points.priority.high"
- value_range: { start: 15, end: 32 }
  load_shed_point: "envoy.load_shed_points.priority.low"
)EOF",
                            config);

  auto config_or = PriorityLoadShedFilterConfig::create(config, overload_manager_, "prefix.",
                                                        *stats_store_.rootScope());
  EXPECT_FALSE(config_or.ok());
  EXPECT_THAT(absl::StrCat(config_or.status()), HasSubstr("bucket ranges overlap"));
}

} // namespace
} // namespace PriorityLoadShed
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
