#include "envoy/extensions/filters/network/direct_response/v3/config.pb.validate.h"

#include "extensions/filters/network/direct_response/filter.h"

#include "test/mocks/api/mocks.h"
#include "test/mocks/network/mocks.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::_;
using testing::NiceMock;

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace DirectResponse {

class DirectResponseFilterTest : public testing::Test {
public:
  void initialize(const std::string& response) {
    filter_ = std::make_shared<DirectResponseFilter>(response);
    filter_->initializeReadFilterCallbacks(read_filter_callbacks_);
  }
  std::shared_ptr<DirectResponseFilter> filter_;
  NiceMock<Network::MockReadFilterCallbacks> read_filter_callbacks_;
};

// Test the filter's onNewConnection() with a non-empty response
TEST_F(DirectResponseFilterTest, OnNewConnection) {
  initialize("hello");
  Buffer::OwnedImpl response("hello");
  EXPECT_CALL(read_filter_callbacks_.connection_, write(BufferEqual(&response), false));
  EXPECT_CALL(read_filter_callbacks_.connection_, close(Network::ConnectionCloseType::FlushWrite));
  EXPECT_CALL(read_filter_callbacks_.connection_.stream_info_,
              setResponseCodeDetails(StreamInfo::ResponseCodeDetails::get().DirectResponse));
  EXPECT_EQ(Network::FilterStatus::StopIteration, filter_->onNewConnection());
}

// Test the filter's onNewConnection() with an empty response
TEST_F(DirectResponseFilterTest, OnNewConnectionEmptyResponse) {
  initialize("");
  EXPECT_CALL(read_filter_callbacks_.connection_, write(_, _)).Times(0);
  EXPECT_CALL(read_filter_callbacks_.connection_, close(Network::ConnectionCloseType::FlushWrite));
  EXPECT_CALL(read_filter_callbacks_.connection_.stream_info_,
              setResponseCodeDetails(StreamInfo::ResponseCodeDetails::get().DirectResponse));
  EXPECT_EQ(Network::FilterStatus::StopIteration, filter_->onNewConnection());
}

// Test the filter's onData()
TEST_F(DirectResponseFilterTest, OnData) {
  initialize("hello");
  Buffer::OwnedImpl data("data");
  EXPECT_CALL(read_filter_callbacks_.connection_, write(_, _)).Times(0);
  EXPECT_EQ(Network::FilterStatus::Continue, filter_->onData(data, false));
}

} // namespace DirectResponse
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
