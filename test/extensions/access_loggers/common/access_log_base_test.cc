#include "extensions/access_loggers/common/access_log_base.h"

#include "test/mocks/access_log/mocks.h"
#include "test/mocks/stream_info/mocks.h"

#include "gmock/gmock.h"

namespace Envoy {
namespace Extensions {
namespace AccessLoggers {
namespace Common {
namespace {

using AccessLog::FilterPtr;
using AccessLog::MockFilter;
using testing::_;
using testing::Return;

class TestImpl : public ImplBase {
public:
  TestImpl(FilterPtr filter) : ImplBase(std::move(filter)) {}

  int count() { return count_; };

private:
  void emitLog(const Http::RequestHeaderMap&, const Http::ResponseHeaderMap&,
               const Http::ResponseTrailerMap&, const StreamInfo::StreamInfo&) override {
    count_++;
  }

  int count_ = 0;
};

TEST(AccessLogBaseTest, NoFilter) {
  StreamInfo::MockStreamInfo stream_info;
  TestImpl logger(nullptr);
  EXPECT_EQ(logger.count(), 0);
  logger.log(nullptr, nullptr, nullptr, stream_info);
  EXPECT_EQ(logger.count(), 1);
}

TEST(AccessLogBaseTest, FilterReject) {
  StreamInfo::MockStreamInfo stream_info;

  std::unique_ptr<MockFilter> filter = std::make_unique<MockFilter>();
  EXPECT_CALL(*filter, evaluate(_, _, _, _)).WillOnce(Return(false));
  TestImpl logger(std::move(filter));
  EXPECT_EQ(logger.count(), 0);
  logger.log(nullptr, nullptr, nullptr, stream_info);
  EXPECT_EQ(logger.count(), 0);
}

} // namespace
} // namespace Common
} // namespace AccessLoggers
} // namespace Extensions
} // namespace Envoy
