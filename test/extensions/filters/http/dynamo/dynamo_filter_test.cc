#include <memory>
#include <string>

#include "common/buffer/buffer_impl.h"
#include "common/http/header_map_impl.h"

#include "extensions/filters/http/dynamo/dynamo_filter.h"

#include "test/mocks/http/mocks.h"
#include "test/mocks/runtime/mocks.h"
#include "test/mocks/stats/mocks.h"
#include "test/test_common/printers.h"
#include "test/test_common/utility.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::NiceMock;
using testing::Property;
using testing::Return;
using testing::ReturnRef;
using testing::_;

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace Dynamo {

class DynamoFilterTest : public testing::Test {
public:
  void setup(bool enabled) {
    ON_CALL(loader_.snapshot_, featureEnabled("dynamodb.filter_enabled", 100))
        .WillByDefault(Return(enabled));
    EXPECT_CALL(loader_.snapshot_, featureEnabled("dynamodb.filter_enabled", 100));

    filter_.reset(new DynamoFilter(loader_, stat_prefix_, stats_));

    filter_->setDecoderFilterCallbacks(decoder_callbacks_);
    filter_->setEncoderFilterCallbacks(encoder_callbacks_);
  }

  ~DynamoFilterTest() { filter_->onDestroy(); }

  std::unique_ptr<DynamoFilter> filter_;
  NiceMock<Runtime::MockLoader> loader_;
  std::string stat_prefix_{"prefix."};
  NiceMock<Stats::MockStore> stats_;
  NiceMock<Http::MockStreamDecoderFilterCallbacks> decoder_callbacks_;
  NiceMock<Http::MockStreamEncoderFilterCallbacks> encoder_callbacks_;
};

TEST_F(DynamoFilterTest, operatorPresent) {
  setup(true);

  Http::TestHeaderMapImpl request_headers{{"x-amz-target", "version.Get"}, {"random", "random"}};

  EXPECT_EQ(Http::FilterHeadersStatus::StopIteration,
            filter_->decodeHeaders(request_headers, true));

  Http::TestHeaderMapImpl continue_headers{{":status", "100"}};
  EXPECT_EQ(Http::FilterHeadersStatus::Continue,
            filter_->encode100ContinueHeaders(continue_headers));

  Http::TestHeaderMapImpl response_headers{{":status", "200"}};
  EXPECT_CALL(stats_, counter("prefix.dynamodb.operation_missing")).Times(0);
  EXPECT_CALL(stats_, counter("prefix.dynamodb.table_missing"));

  EXPECT_CALL(stats_, counter("prefix.dynamodb.operation.Get.upstream_rq_total_2xx"));
  EXPECT_CALL(stats_, counter("prefix.dynamodb.operation.Get.upstream_rq_total_200"));
  EXPECT_CALL(stats_, counter("prefix.dynamodb.operation.Get.upstream_rq_total"));

  EXPECT_CALL(stats_, histogram("prefix.dynamodb.operation.Get.upstream_rq_time_2xx"));
  EXPECT_CALL(stats_, histogram("prefix.dynamodb.operation.Get.upstream_rq_time_200"));
  EXPECT_CALL(stats_, histogram("prefix.dynamodb.operation.Get.upstream_rq_time"));
  EXPECT_CALL(
      stats_,
      deliverHistogramToSinks(
          Property(&Stats::Metric::name, "prefix.dynamodb.operation.Get.upstream_rq_time_2xx"), _));
  EXPECT_CALL(
      stats_,
      deliverHistogramToSinks(
          Property(&Stats::Metric::name, "prefix.dynamodb.operation.Get.upstream_rq_time_200"), _));
  EXPECT_CALL(
      stats_,
      deliverHistogramToSinks(
          Property(&Stats::Metric::name, "prefix.dynamodb.operation.Get.upstream_rq_time"), _));

  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->encodeHeaders(response_headers, true));
}

TEST_F(DynamoFilterTest, jsonBodyNotWellFormed) {
  setup(true);

  Http::TestHeaderMapImpl request_headers{{"x-amz-target", "version.GetItem"},
                                          {"random", "random"}};
  EXPECT_EQ(Http::FilterHeadersStatus::StopIteration,
            filter_->decodeHeaders(request_headers, false));

  Buffer::OwnedImpl buffer;
  buffer.add("test", 4);
  buffer.add("test2", 5);

  EXPECT_CALL(stats_, counter("prefix.dynamodb.invalid_req_body"));
  EXPECT_EQ(Http::FilterDataStatus::Continue, filter_->decodeData(buffer, true));
}

TEST_F(DynamoFilterTest, bothOperationAndTableIncorrect) {
  setup(true);

  Http::TestHeaderMapImpl request_headers{{"x-amz-target", "version"}, {"random", "random"}};
  EXPECT_EQ(Http::FilterHeadersStatus::StopIteration,
            filter_->decodeHeaders(request_headers, true));

  EXPECT_CALL(stats_, counter("prefix.dynamodb.operation_missing"));
  EXPECT_CALL(stats_, counter("prefix.dynamodb.table_missing"));

  Http::TestHeaderMapImpl response_headers{{":status", "200"}};
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->encodeHeaders(response_headers, true));
}

TEST_F(DynamoFilterTest, handleErrorTypeTableMissing) {
  setup(true);

  Http::TestHeaderMapImpl request_headers{{"x-amz-target", "version"}, {"random", "random"}};
  EXPECT_EQ(Http::FilterHeadersStatus::StopIteration,
            filter_->decodeHeaders(request_headers, true));

  EXPECT_CALL(stats_, counter("prefix.dynamodb.operation_missing"));
  EXPECT_CALL(stats_, counter("prefix.dynamodb.table_missing"));

  Http::TestHeaderMapImpl response_headers{{":status", "400"}};
  EXPECT_EQ(Http::FilterHeadersStatus::StopIteration,
            filter_->encodeHeaders(response_headers, false));

  Buffer::InstancePtr error_data(new Buffer::OwnedImpl());
  std::string internal_error =
      "{\"__type\":\"com.amazonaws.dynamodb.v20120810#ValidationException\"}";
  error_data->add(internal_error);
  EXPECT_CALL(stats_, counter("prefix.dynamodb.error.no_table.ValidationException"));
  EXPECT_EQ(Http::FilterDataStatus::Continue, filter_->encodeData(*error_data, true));

  error_data->add("}", 1);
  EXPECT_EQ(Http::FilterDataStatus::StopIterationAndBuffer,
            filter_->encodeData(*error_data, false));
  EXPECT_CALL(encoder_callbacks_, encodingBuffer()).WillRepeatedly(Return(error_data.get()));
  EXPECT_CALL(stats_, counter("prefix.dynamodb.invalid_resp_body"));
  EXPECT_CALL(stats_, counter("prefix.dynamodb.operation_missing"));
  EXPECT_CALL(stats_, counter("prefix.dynamodb.table_missing"));
  EXPECT_EQ(Http::FilterTrailersStatus::Continue, filter_->encodeTrailers(request_headers));
}

TEST_F(DynamoFilterTest, HandleErrorTypeTablePresent) {
  setup(true);

  Http::TestHeaderMapImpl request_headers{{"x-amz-target", "version.GetItem"},
                                          {"random", "random"}};
  EXPECT_EQ(Http::FilterHeadersStatus::StopIteration,
            filter_->decodeHeaders(request_headers, false));

  Buffer::OwnedImpl buffer;
  std::string buffer_content = "{\"TableName\":\"locations\"}";
  buffer.add(buffer_content);
  EXPECT_EQ(Http::FilterDataStatus::Continue, filter_->decodeData(buffer, true));

  Http::TestHeaderMapImpl response_headers{{":status", "400"}};
  EXPECT_EQ(Http::FilterHeadersStatus::StopIteration,
            filter_->encodeHeaders(response_headers, false));

  Buffer::OwnedImpl error_data;
  std::string internal_error =
      "{\"__type\":\"com.amazonaws.dynamodb.v20120810#ValidationException\"}";
  error_data.add(internal_error);
  EXPECT_CALL(stats_, counter("prefix.dynamodb.error.locations.ValidationException"));

  EXPECT_CALL(stats_, counter("prefix.dynamodb.operation.GetItem.upstream_rq_total"));
  EXPECT_CALL(stats_, counter("prefix.dynamodb.operation.GetItem.upstream_rq_total_4xx"));
  EXPECT_CALL(stats_, counter("prefix.dynamodb.operation.GetItem.upstream_rq_total_400"));

  EXPECT_CALL(stats_, histogram("prefix.dynamodb.operation.GetItem.upstream_rq_time"));
  EXPECT_CALL(stats_, histogram("prefix.dynamodb.operation.GetItem.upstream_rq_time_4xx"));
  EXPECT_CALL(stats_, histogram("prefix.dynamodb.operation.GetItem.upstream_rq_time_400"));
  EXPECT_CALL(stats_, deliverHistogramToSinks(
                          Property(&Stats::Metric::name,
                                   "prefix.dynamodb.operation.GetItem.upstream_rq_time_4xx"),
                          _));
  EXPECT_CALL(stats_, deliverHistogramToSinks(
                          Property(&Stats::Metric::name,
                                   "prefix.dynamodb.operation.GetItem.upstream_rq_time_400"),
                          _));
  EXPECT_CALL(
      stats_,
      deliverHistogramToSinks(
          Property(&Stats::Metric::name, "prefix.dynamodb.operation.GetItem.upstream_rq_time"), _));

  EXPECT_CALL(stats_, counter("prefix.dynamodb.table.locations.upstream_rq_total_4xx"));
  EXPECT_CALL(stats_, counter("prefix.dynamodb.table.locations.upstream_rq_total_400"));
  EXPECT_CALL(stats_, counter("prefix.dynamodb.table.locations.upstream_rq_total"));

  EXPECT_CALL(stats_, histogram("prefix.dynamodb.table.locations.upstream_rq_time_4xx"));
  EXPECT_CALL(stats_, histogram("prefix.dynamodb.table.locations.upstream_rq_time_400"));
  EXPECT_CALL(stats_, histogram("prefix.dynamodb.table.locations.upstream_rq_time"));
  EXPECT_CALL(stats_, deliverHistogramToSinks(
                          Property(&Stats::Metric::name,
                                   "prefix.dynamodb.table.locations.upstream_rq_time_4xx"),
                          _));
  EXPECT_CALL(stats_, deliverHistogramToSinks(
                          Property(&Stats::Metric::name,
                                   "prefix.dynamodb.table.locations.upstream_rq_time_400"),
                          _));
  EXPECT_CALL(
      stats_,
      deliverHistogramToSinks(
          Property(&Stats::Metric::name, "prefix.dynamodb.table.locations.upstream_rq_time"), _));

  EXPECT_EQ(Http::FilterDataStatus::Continue, filter_->encodeData(error_data, true));
}

TEST_F(DynamoFilterTest, BatchMultipleTables) {
  setup(true);

  Http::TestHeaderMapImpl request_headers{{"x-amz-target", "version.BatchGetItem"},
                                          {"random", "random"}};
  EXPECT_EQ(Http::FilterHeadersStatus::StopIteration,
            filter_->decodeHeaders(request_headers, false));

  Buffer::InstancePtr buffer(new Buffer::OwnedImpl());
  std::string buffer_content = R"EOF(
{
  "RequestItems": {
    "table_1": { "test1" : "something" },
    "table_2": { "test2" : "something" }
  }
}
)EOF";
  buffer->add(buffer_content);

  EXPECT_EQ(Http::FilterDataStatus::StopIterationAndBuffer, filter_->decodeData(*buffer, false));
  EXPECT_CALL(decoder_callbacks_, decodingBuffer()).WillRepeatedly(Return(buffer.get()));
  EXPECT_EQ(Http::FilterTrailersStatus::Continue, filter_->decodeTrailers(request_headers));

  Http::TestHeaderMapImpl response_headers{{":status", "200"}};
  EXPECT_CALL(stats_, counter("prefix.dynamodb.multiple_tables"));

  EXPECT_CALL(stats_, counter("prefix.dynamodb.operation.BatchGetItem.upstream_rq_total"));
  EXPECT_CALL(stats_, counter("prefix.dynamodb.operation.BatchGetItem.upstream_rq_total_2xx"));
  EXPECT_CALL(stats_, counter("prefix.dynamodb.operation.BatchGetItem.upstream_rq_total_200"));

  EXPECT_CALL(stats_, histogram("prefix.dynamodb.operation.BatchGetItem.upstream_rq_time"));
  EXPECT_CALL(stats_, histogram("prefix.dynamodb.operation.BatchGetItem.upstream_rq_time_2xx"));
  EXPECT_CALL(stats_, histogram("prefix.dynamodb.operation.BatchGetItem.upstream_rq_time_200"));
  EXPECT_CALL(stats_, deliverHistogramToSinks(
                          Property(&Stats::Metric::name,
                                   "prefix.dynamodb.operation.BatchGetItem.upstream_rq_time_2xx"),
                          _));
  EXPECT_CALL(stats_, deliverHistogramToSinks(
                          Property(&Stats::Metric::name,
                                   "prefix.dynamodb.operation.BatchGetItem.upstream_rq_time_200"),
                          _));
  EXPECT_CALL(stats_, deliverHistogramToSinks(
                          Property(&Stats::Metric::name,
                                   "prefix.dynamodb.operation.BatchGetItem.upstream_rq_time"),
                          _));

  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->encodeHeaders(response_headers, true));
}

TEST_F(DynamoFilterTest, BatchMultipleTablesUnprocessedKeys) {
  setup(true);

  Http::TestHeaderMapImpl request_headers{{"x-amz-target", "version.BatchGetItem"},
                                          {"random", "random"}};
  EXPECT_EQ(Http::FilterHeadersStatus::StopIteration,
            filter_->decodeHeaders(request_headers, false));

  Buffer::InstancePtr buffer(new Buffer::OwnedImpl());
  std::string buffer_content = R"EOF(
{
  "RequestItems": {
    "table_1": { "test1" : "something" },
    "table_2": { "test2" : "something" }
  }
}
)EOF";
  buffer->add(buffer_content);

  EXPECT_EQ(Http::FilterDataStatus::StopIterationAndBuffer, filter_->decodeData(*buffer, false));
  EXPECT_CALL(decoder_callbacks_, decodingBuffer()).WillRepeatedly(Return(buffer.get()));
  EXPECT_EQ(Http::FilterTrailersStatus::Continue, filter_->decodeTrailers(request_headers));

  Http::TestHeaderMapImpl response_headers{{":status", "200"}};
  EXPECT_CALL(stats_, counter("prefix.dynamodb.multiple_tables"));

  EXPECT_CALL(stats_, counter("prefix.dynamodb.operation.BatchGetItem.upstream_rq_total"));
  EXPECT_CALL(stats_, counter("prefix.dynamodb.operation.BatchGetItem.upstream_rq_total_2xx"));
  EXPECT_CALL(stats_, counter("prefix.dynamodb.operation.BatchGetItem.upstream_rq_total_200"));

  EXPECT_CALL(stats_, histogram("prefix.dynamodb.operation.BatchGetItem.upstream_rq_time"));
  EXPECT_CALL(stats_, histogram("prefix.dynamodb.operation.BatchGetItem.upstream_rq_time_2xx"));
  EXPECT_CALL(stats_, histogram("prefix.dynamodb.operation.BatchGetItem.upstream_rq_time_200"));
  EXPECT_CALL(stats_, deliverHistogramToSinks(
                          Property(&Stats::Metric::name,
                                   "prefix.dynamodb.operation.BatchGetItem.upstream_rq_time_2xx"),
                          _));
  EXPECT_CALL(stats_, deliverHistogramToSinks(
                          Property(&Stats::Metric::name,
                                   "prefix.dynamodb.operation.BatchGetItem.upstream_rq_time_200"),
                          _));
  EXPECT_CALL(stats_, deliverHistogramToSinks(
                          Property(&Stats::Metric::name,
                                   "prefix.dynamodb.operation.BatchGetItem.upstream_rq_time"),
                          _));

  EXPECT_EQ(Http::FilterHeadersStatus::StopIteration,
            filter_->encodeHeaders(response_headers, false));

  Buffer::OwnedImpl empty_data;
  Buffer::InstancePtr response_data(new Buffer::OwnedImpl());
  std::string response_content = R"EOF(
{
  "UnprocessedKeys": {
    "table_1": { "test1" : "something" },
    "table_2": { "test2" : "something" }
  }
}
)EOF";
  response_data->add(response_content);

  EXPECT_CALL(stats_, counter("prefix.dynamodb.error.table_1.BatchFailureUnprocessedKeys"));
  EXPECT_CALL(stats_, counter("prefix.dynamodb.error.table_2.BatchFailureUnprocessedKeys"));
  EXPECT_CALL(encoder_callbacks_, encodingBuffer()).WillRepeatedly(Return(response_data.get()));
  EXPECT_EQ(Http::FilterDataStatus::Continue, filter_->encodeData(empty_data, true));
}

TEST_F(DynamoFilterTest, BatchMultipleTablesNoUnprocessedKeys) {
  setup(true);

  Http::TestHeaderMapImpl request_headers{{"x-amz-target", "version.BatchGetItem"},
                                          {"random", "random"}};
  EXPECT_EQ(Http::FilterHeadersStatus::StopIteration,
            filter_->decodeHeaders(request_headers, false));

  Buffer::InstancePtr buffer(new Buffer::OwnedImpl());
  std::string buffer_content = R"EOF(
{
  "RequestItems": {
    "table_1": { "test1" : "something" },
    "table_2": { "test2" : "something" }
  }
}
)EOF";
  buffer->add(buffer_content);

  EXPECT_EQ(Http::FilterDataStatus::StopIterationAndBuffer, filter_->decodeData(*buffer, false));
  EXPECT_CALL(decoder_callbacks_, decodingBuffer()).WillRepeatedly(Return(buffer.get()));
  EXPECT_EQ(Http::FilterTrailersStatus::Continue, filter_->decodeTrailers(request_headers));

  Http::TestHeaderMapImpl response_headers{{":status", "200"}};
  EXPECT_CALL(stats_, counter("prefix.dynamodb.multiple_tables"));

  EXPECT_CALL(stats_, counter("prefix.dynamodb.operation.BatchGetItem.upstream_rq_total"));
  EXPECT_CALL(stats_, counter("prefix.dynamodb.operation.BatchGetItem.upstream_rq_total_2xx"));
  EXPECT_CALL(stats_, counter("prefix.dynamodb.operation.BatchGetItem.upstream_rq_total_200"));

  EXPECT_CALL(stats_, histogram("prefix.dynamodb.operation.BatchGetItem.upstream_rq_time"));
  EXPECT_CALL(stats_, histogram("prefix.dynamodb.operation.BatchGetItem.upstream_rq_time_2xx"));
  EXPECT_CALL(stats_, histogram("prefix.dynamodb.operation.BatchGetItem.upstream_rq_time_200"));
  EXPECT_CALL(stats_, deliverHistogramToSinks(
                          Property(&Stats::Metric::name,
                                   "prefix.dynamodb.operation.BatchGetItem.upstream_rq_time_2xx"),
                          _));
  EXPECT_CALL(stats_, deliverHistogramToSinks(
                          Property(&Stats::Metric::name,
                                   "prefix.dynamodb.operation.BatchGetItem.upstream_rq_time_200"),
                          _));
  EXPECT_CALL(stats_, deliverHistogramToSinks(
                          Property(&Stats::Metric::name,
                                   "prefix.dynamodb.operation.BatchGetItem.upstream_rq_time"),
                          _));

  EXPECT_EQ(Http::FilterHeadersStatus::StopIteration,
            filter_->encodeHeaders(response_headers, false));

  Buffer::OwnedImpl empty_data;
  Buffer::InstancePtr response_data(new Buffer::OwnedImpl());
  std::string response_content = R"EOF(
{
  "UnprocessedKeys": {
  }
}
)EOF";
  response_data->add(response_content);

  EXPECT_CALL(encoder_callbacks_, encodingBuffer()).WillOnce(Return(response_data.get()));
  EXPECT_EQ(Http::FilterDataStatus::Continue, filter_->encodeData(empty_data, true));
}

TEST_F(DynamoFilterTest, BatchMultipleTablesInvalidResponseBody) {
  setup(true);

  Http::TestHeaderMapImpl request_headers{{"x-amz-target", "version.BatchGetItem"},
                                          {"random", "random"}};
  EXPECT_EQ(Http::FilterHeadersStatus::StopIteration,
            filter_->decodeHeaders(request_headers, false));

  Buffer::InstancePtr buffer(new Buffer::OwnedImpl());
  std::string buffer_content = R"EOF(
{
  "RequestItems": {
    "table_1": { "test1" : "something" },
    "table_2": { "test2" : "something" }
  }
}
)EOF";
  buffer->add(buffer_content);

  EXPECT_EQ(Http::FilterDataStatus::StopIterationAndBuffer, filter_->decodeData(*buffer, false));
  EXPECT_CALL(decoder_callbacks_, decodingBuffer()).WillRepeatedly(Return(buffer.get()));
  EXPECT_EQ(Http::FilterTrailersStatus::Continue, filter_->decodeTrailers(request_headers));

  Http::TestHeaderMapImpl response_headers{{":status", "200"}};
  EXPECT_CALL(stats_, counter("prefix.dynamodb.multiple_tables"));

  EXPECT_CALL(stats_, counter("prefix.dynamodb.operation.BatchGetItem.upstream_rq_total"));
  EXPECT_CALL(stats_, counter("prefix.dynamodb.operation.BatchGetItem.upstream_rq_total_2xx"));
  EXPECT_CALL(stats_, counter("prefix.dynamodb.operation.BatchGetItem.upstream_rq_total_200"));

  EXPECT_CALL(stats_, histogram("prefix.dynamodb.operation.BatchGetItem.upstream_rq_time"));
  EXPECT_CALL(stats_, histogram("prefix.dynamodb.operation.BatchGetItem.upstream_rq_time_2xx"));
  EXPECT_CALL(stats_, histogram("prefix.dynamodb.operation.BatchGetItem.upstream_rq_time_200"));
  EXPECT_CALL(stats_, deliverHistogramToSinks(
                          Property(&Stats::Metric::name,
                                   "prefix.dynamodb.operation.BatchGetItem.upstream_rq_time_2xx"),
                          _));
  EXPECT_CALL(stats_, deliverHistogramToSinks(
                          Property(&Stats::Metric::name,
                                   "prefix.dynamodb.operation.BatchGetItem.upstream_rq_time_200"),
                          _));
  EXPECT_CALL(stats_, deliverHistogramToSinks(
                          Property(&Stats::Metric::name,
                                   "prefix.dynamodb.operation.BatchGetItem.upstream_rq_time"),
                          _));

  EXPECT_EQ(Http::FilterHeadersStatus::StopIteration,
            filter_->encodeHeaders(response_headers, false));

  Buffer::OwnedImpl empty_data;
  Buffer::InstancePtr response_data(new Buffer::OwnedImpl());
  std::string response_content = R"EOF(
{
  "UnprocessedKeys": {
    "table_1": { "test1" : "something" },
    "table_2": { "test2" : "something" }
  }
}
)EOF";
  response_data->add(response_content);
  response_data->add("}", 1);

  EXPECT_CALL(stats_, counter("prefix.dynamodb.invalid_resp_body"));
  EXPECT_CALL(encoder_callbacks_, encodingBuffer()).WillOnce(Return(response_data.get()));
  EXPECT_EQ(Http::FilterDataStatus::Continue, filter_->encodeData(empty_data, true));
}

TEST_F(DynamoFilterTest, bothOperationAndTableCorrect) {
  setup(true);

  Http::TestHeaderMapImpl request_headers{{"x-amz-target", "version.GetItem"}};
  Buffer::InstancePtr buffer(new Buffer::OwnedImpl());
  std::string buffer_content = "{\"TableName\":\"locations\"";
  buffer->add(buffer_content);
  EXPECT_CALL(decoder_callbacks_, decodingBuffer()).WillRepeatedly(Return(buffer.get()));
  Buffer::OwnedImpl data;
  data.add("}", 1);

  EXPECT_EQ(Http::FilterHeadersStatus::StopIteration,
            filter_->decodeHeaders(request_headers, false));
  EXPECT_EQ(Http::FilterDataStatus::StopIterationAndBuffer, filter_->decodeData(data, false));
  EXPECT_EQ(Http::FilterDataStatus::Continue, filter_->decodeData(data, true));

  EXPECT_CALL(stats_, counter("prefix.dynamodb.operation.GetItem.upstream_rq_total_2xx"));
  EXPECT_CALL(stats_, counter("prefix.dynamodb.operation.GetItem.upstream_rq_total_200"));
  EXPECT_CALL(stats_, counter("prefix.dynamodb.operation.GetItem.upstream_rq_total"));

  EXPECT_CALL(stats_, histogram("prefix.dynamodb.operation.GetItem.upstream_rq_time_2xx"));
  EXPECT_CALL(stats_, histogram("prefix.dynamodb.operation.GetItem.upstream_rq_time_200"));
  EXPECT_CALL(stats_, histogram("prefix.dynamodb.operation.GetItem.upstream_rq_time"));
  EXPECT_CALL(stats_, deliverHistogramToSinks(
                          Property(&Stats::Metric::name,
                                   "prefix.dynamodb.operation.GetItem.upstream_rq_time_2xx"),
                          _));
  EXPECT_CALL(stats_, deliverHistogramToSinks(
                          Property(&Stats::Metric::name,
                                   "prefix.dynamodb.operation.GetItem.upstream_rq_time_200"),
                          _));
  EXPECT_CALL(
      stats_,
      deliverHistogramToSinks(
          Property(&Stats::Metric::name, "prefix.dynamodb.operation.GetItem.upstream_rq_time"), _));

  EXPECT_CALL(stats_, counter("prefix.dynamodb.table.locations.upstream_rq_total_2xx"));
  EXPECT_CALL(stats_, counter("prefix.dynamodb.table.locations.upstream_rq_total_200"));
  EXPECT_CALL(stats_, counter("prefix.dynamodb.table.locations.upstream_rq_total"));

  EXPECT_CALL(stats_, histogram("prefix.dynamodb.table.locations.upstream_rq_time_2xx"));
  EXPECT_CALL(stats_, histogram("prefix.dynamodb.table.locations.upstream_rq_time_200"));
  EXPECT_CALL(stats_, histogram("prefix.dynamodb.table.locations.upstream_rq_time"));
  EXPECT_CALL(stats_, deliverHistogramToSinks(
                          Property(&Stats::Metric::name,
                                   "prefix.dynamodb.table.locations.upstream_rq_time_2xx"),
                          _));
  EXPECT_CALL(stats_, deliverHistogramToSinks(
                          Property(&Stats::Metric::name,
                                   "prefix.dynamodb.table.locations.upstream_rq_time_200"),
                          _));
  EXPECT_CALL(
      stats_,
      deliverHistogramToSinks(
          Property(&Stats::Metric::name, "prefix.dynamodb.table.locations.upstream_rq_time"), _));

  Http::TestHeaderMapImpl response_headers{{":status", "200"}};
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->encodeHeaders(response_headers, true));
}

TEST_F(DynamoFilterTest, operatorPresentRuntimeDisabled) {
  setup(false);

  EXPECT_CALL(stats_, counter(_)).Times(0);
  EXPECT_CALL(stats_, deliverHistogramToSinks(_, _)).Times(0);

  Http::TestHeaderMapImpl request_headers{{"x-amz-target", "version.operator"},
                                          {"random", "random"}};
  Http::TestHeaderMapImpl response_headers{{":status", "200"}};

  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->decodeHeaders(request_headers, true));
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->encodeHeaders(response_headers, true));
  EXPECT_EQ(Http::FilterTrailersStatus::Continue, filter_->encodeTrailers(response_headers));
}

TEST_F(DynamoFilterTest, PartitionIdStats) {
  setup(true);

  Http::TestHeaderMapImpl request_headers{{"x-amz-target", "version.GetItem"}};
  Buffer::InstancePtr buffer(new Buffer::OwnedImpl());
  std::string buffer_content = "{\"TableName\":\"locations\"";
  buffer->add(buffer_content);
  ON_CALL(decoder_callbacks_, decodingBuffer()).WillByDefault(Return(buffer.get()));
  Buffer::OwnedImpl data;
  data.add("}", 1);

  EXPECT_EQ(Http::FilterHeadersStatus::StopIteration,
            filter_->decodeHeaders(request_headers, false));
  EXPECT_EQ(Http::FilterDataStatus::StopIterationAndBuffer, filter_->decodeData(data, false));
  EXPECT_EQ(Http::FilterDataStatus::Continue, filter_->decodeData(data, true));

  EXPECT_CALL(stats_, counter("prefix.dynamodb.operation.GetItem.upstream_rq_total_2xx"));
  EXPECT_CALL(stats_, counter("prefix.dynamodb.operation.GetItem.upstream_rq_total_200"));
  EXPECT_CALL(stats_, counter("prefix.dynamodb.operation.GetItem.upstream_rq_total"));

  EXPECT_CALL(stats_, histogram("prefix.dynamodb.operation.GetItem.upstream_rq_time_2xx"));
  EXPECT_CALL(stats_, histogram("prefix.dynamodb.operation.GetItem.upstream_rq_time_200"));
  EXPECT_CALL(stats_, histogram("prefix.dynamodb.operation.GetItem.upstream_rq_time"));
  EXPECT_CALL(stats_, deliverHistogramToSinks(
                          Property(&Stats::Metric::name,
                                   "prefix.dynamodb.operation.GetItem.upstream_rq_time_2xx"),
                          _));
  EXPECT_CALL(stats_, deliverHistogramToSinks(
                          Property(&Stats::Metric::name,
                                   "prefix.dynamodb.operation.GetItem.upstream_rq_time_200"),
                          _));
  EXPECT_CALL(
      stats_,
      deliverHistogramToSinks(
          Property(&Stats::Metric::name, "prefix.dynamodb.operation.GetItem.upstream_rq_time"), _));

  EXPECT_CALL(stats_, counter("prefix.dynamodb.table.locations.upstream_rq_total_2xx"));
  EXPECT_CALL(stats_, counter("prefix.dynamodb.table.locations.upstream_rq_total_200"));
  EXPECT_CALL(stats_, counter("prefix.dynamodb.table.locations.upstream_rq_total"));

  EXPECT_CALL(stats_, histogram("prefix.dynamodb.table.locations.upstream_rq_time_2xx"));
  EXPECT_CALL(stats_, histogram("prefix.dynamodb.table.locations.upstream_rq_time_200"));
  EXPECT_CALL(stats_, histogram("prefix.dynamodb.table.locations.upstream_rq_time"));
  EXPECT_CALL(stats_, deliverHistogramToSinks(
                          Property(&Stats::Metric::name,
                                   "prefix.dynamodb.table.locations.upstream_rq_time_2xx"),
                          _));
  EXPECT_CALL(stats_, deliverHistogramToSinks(
                          Property(&Stats::Metric::name,
                                   "prefix.dynamodb.table.locations.upstream_rq_time_200"),
                          _));
  EXPECT_CALL(
      stats_,
      deliverHistogramToSinks(
          Property(&Stats::Metric::name, "prefix.dynamodb.table.locations.upstream_rq_time"), _));

  EXPECT_CALL(stats_,
              counter("prefix.dynamodb.table.locations.capacity.GetItem.__partition_id=ition_1"))
      .Times(1);
  EXPECT_CALL(stats_,
              counter("prefix.dynamodb.table.locations.capacity.GetItem.__partition_id=ition_2"))
      .Times(1);

  Http::TestHeaderMapImpl response_headers{{":status", "200"}};
  EXPECT_EQ(Http::FilterHeadersStatus::StopIteration,
            filter_->encodeHeaders(response_headers, false));

  Buffer::OwnedImpl empty_data;
  Buffer::InstancePtr response_data(new Buffer::OwnedImpl());
  std::string response_content = R"EOF(
    {
      "ConsumedCapacity": {
        "Partitions": {
          "partition_1" : 0.5,
          "partition_2" : 3.0
        }
      }
    }
    )EOF";

  response_data->add(response_content);

  EXPECT_CALL(encoder_callbacks_, encodingBuffer()).WillOnce(Return(response_data.get()));
  EXPECT_EQ(Http::FilterDataStatus::Continue, filter_->encodeData(empty_data, true));
}

TEST_F(DynamoFilterTest, NoPartitionIdStatsForMultipleTables) {
  setup(true);

  Http::TestHeaderMapImpl request_headers{{"x-amz-target", "version.BatchGetItem"}};
  Buffer::InstancePtr buffer(new Buffer::OwnedImpl());
  std::string buffer_content = R"EOF(
{
  "RequestItems": {
    "table_1": { "test1" : "something" },
    "table_2": { "test2" : "something" }
  }
}
)EOF";
  buffer->add(buffer_content);
  ON_CALL(decoder_callbacks_, decodingBuffer()).WillByDefault(Return(buffer.get()));

  EXPECT_EQ(Http::FilterHeadersStatus::StopIteration,
            filter_->decodeHeaders(request_headers, false));
  EXPECT_EQ(Http::FilterDataStatus::StopIterationAndBuffer, filter_->decodeData(*buffer, false));
  EXPECT_EQ(Http::FilterTrailersStatus::Continue, filter_->decodeTrailers(request_headers));

  EXPECT_CALL(stats_, counter("prefix.dynamodb.multiple_tables"));

  EXPECT_CALL(stats_, counter("prefix.dynamodb.operation.BatchGetItem.upstream_rq_total"));
  EXPECT_CALL(stats_, counter("prefix.dynamodb.operation.BatchGetItem.upstream_rq_total_2xx"));
  EXPECT_CALL(stats_, counter("prefix.dynamodb.operation.BatchGetItem.upstream_rq_total_200"));

  EXPECT_CALL(stats_, histogram("prefix.dynamodb.operation.BatchGetItem.upstream_rq_time"));
  EXPECT_CALL(stats_, histogram("prefix.dynamodb.operation.BatchGetItem.upstream_rq_time_2xx"));
  EXPECT_CALL(stats_, histogram("prefix.dynamodb.operation.BatchGetItem.upstream_rq_time_200"));
  EXPECT_CALL(stats_, deliverHistogramToSinks(
                          Property(&Stats::Metric::name,
                                   "prefix.dynamodb.operation.BatchGetItem.upstream_rq_time_2xx"),
                          _));
  EXPECT_CALL(stats_, deliverHistogramToSinks(
                          Property(&Stats::Metric::name,
                                   "prefix.dynamodb.operation.BatchGetItem.upstream_rq_time_200"),
                          _));
  EXPECT_CALL(stats_, deliverHistogramToSinks(
                          Property(&Stats::Metric::name,
                                   "prefix.dynamodb.operation.BatchGetItem.upstream_rq_time"),
                          _));

  EXPECT_CALL(
      stats_,
      counter("prefix.dynamodb.table.locations.capacity.BatchGetItem.__partition_id=ition_1"))
      .Times(0);
  EXPECT_CALL(
      stats_,
      counter("prefix.dynamodb.table.locations.capacity.BatchGetItem.__partition_id=ition_2"))
      .Times(0);

  Http::TestHeaderMapImpl response_headers{{":status", "200"}};
  EXPECT_EQ(Http::FilterHeadersStatus::StopIteration,
            filter_->encodeHeaders(response_headers, false));

  Buffer::OwnedImpl empty_data;
  Buffer::InstancePtr response_data(new Buffer::OwnedImpl());
  std::string response_content = R"EOF(
    {
      "ConsumedCapacity": {
        "Partitions": {
          "partition_1" : 0.5,
          "partition_2" : 3.0
        }
      }
    }
    )EOF";

  response_data->add(response_content);

  EXPECT_CALL(encoder_callbacks_, encodingBuffer()).WillOnce(Return(response_data.get()));
  EXPECT_EQ(Http::FilterDataStatus::Continue, filter_->encodeData(empty_data, true));
}

TEST_F(DynamoFilterTest, PartitionIdStatsForSingleTableBatchOperation) {
  setup(true);

  Http::TestHeaderMapImpl request_headers{{"x-amz-target", "version.BatchGetItem"}};
  Buffer::InstancePtr buffer(new Buffer::OwnedImpl());
  std::string buffer_content = R"EOF(
{
  "RequestItems": {
    "locations": { "test1" : "something" }
  }
}
)EOF";
  buffer->add(buffer_content);
  ON_CALL(decoder_callbacks_, decodingBuffer()).WillByDefault(Return(buffer.get()));

  EXPECT_EQ(Http::FilterHeadersStatus::StopIteration,
            filter_->decodeHeaders(request_headers, false));
  EXPECT_EQ(Http::FilterDataStatus::StopIterationAndBuffer, filter_->decodeData(*buffer, false));
  EXPECT_EQ(Http::FilterTrailersStatus::Continue, filter_->decodeTrailers(request_headers));

  EXPECT_CALL(stats_, counter("prefix.dynamodb.multiple_tables")).Times(0);

  EXPECT_CALL(stats_, counter("prefix.dynamodb.operation.BatchGetItem.upstream_rq_total"));
  EXPECT_CALL(stats_, counter("prefix.dynamodb.operation.BatchGetItem.upstream_rq_total_2xx"));
  EXPECT_CALL(stats_, counter("prefix.dynamodb.operation.BatchGetItem.upstream_rq_total_200"));

  EXPECT_CALL(stats_, histogram("prefix.dynamodb.operation.BatchGetItem.upstream_rq_time"));
  EXPECT_CALL(stats_, histogram("prefix.dynamodb.operation.BatchGetItem.upstream_rq_time_2xx"));
  EXPECT_CALL(stats_, histogram("prefix.dynamodb.operation.BatchGetItem.upstream_rq_time_200"));
  EXPECT_CALL(stats_, deliverHistogramToSinks(
                          Property(&Stats::Metric::name,
                                   "prefix.dynamodb.operation.BatchGetItem.upstream_rq_time_2xx"),
                          _));
  EXPECT_CALL(stats_, deliverHistogramToSinks(
                          Property(&Stats::Metric::name,
                                   "prefix.dynamodb.operation.BatchGetItem.upstream_rq_time_200"),
                          _));
  EXPECT_CALL(stats_, deliverHistogramToSinks(
                          Property(&Stats::Metric::name,
                                   "prefix.dynamodb.operation.BatchGetItem.upstream_rq_time"),
                          _));

  EXPECT_CALL(stats_, counter("prefix.dynamodb.table.locations.upstream_rq_total_2xx"));
  EXPECT_CALL(stats_, counter("prefix.dynamodb.table.locations.upstream_rq_total_200"));
  EXPECT_CALL(stats_, counter("prefix.dynamodb.table.locations.upstream_rq_total"));

  EXPECT_CALL(stats_, histogram("prefix.dynamodb.table.locations.upstream_rq_time_2xx"));
  EXPECT_CALL(stats_, histogram("prefix.dynamodb.table.locations.upstream_rq_time_200"));
  EXPECT_CALL(stats_, histogram("prefix.dynamodb.table.locations.upstream_rq_time"));
  EXPECT_CALL(stats_, deliverHistogramToSinks(
                          Property(&Stats::Metric::name,
                                   "prefix.dynamodb.table.locations.upstream_rq_time_2xx"),
                          _));
  EXPECT_CALL(stats_, deliverHistogramToSinks(
                          Property(&Stats::Metric::name,
                                   "prefix.dynamodb.table.locations.upstream_rq_time_200"),
                          _));
  EXPECT_CALL(
      stats_,
      deliverHistogramToSinks(
          Property(&Stats::Metric::name, "prefix.dynamodb.table.locations.upstream_rq_time"), _));

  EXPECT_CALL(
      stats_,
      counter("prefix.dynamodb.table.locations.capacity.BatchGetItem.__partition_id=ition_1"))
      .Times(1);
  EXPECT_CALL(
      stats_,
      counter("prefix.dynamodb.table.locations.capacity.BatchGetItem.__partition_id=ition_2"))
      .Times(1);

  Http::TestHeaderMapImpl response_headers{{":status", "200"}};
  EXPECT_EQ(Http::FilterHeadersStatus::StopIteration,
            filter_->encodeHeaders(response_headers, false));

  Buffer::OwnedImpl empty_data;
  Buffer::InstancePtr response_data(new Buffer::OwnedImpl());
  std::string response_content = R"EOF(
    {
      "ConsumedCapacity": {
        "Partitions": {
          "partition_1" : 0.5,
          "partition_2" : 3.0
        }
      }
    }
    )EOF";

  response_data->add(response_content);

  EXPECT_CALL(encoder_callbacks_, encodingBuffer()).WillOnce(Return(response_data.get()));
  EXPECT_EQ(Http::FilterDataStatus::Continue, filter_->encodeData(empty_data, true));
}

} // namespace Dynamo
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
