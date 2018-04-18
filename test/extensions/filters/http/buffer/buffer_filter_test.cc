#include <chrono>
#include <memory>

#include "envoy/config/filter/http/buffer/v2/buffer.pb.h"
#include "envoy/event/dispatcher.h"

#include "common/http/header_map_impl.h"
#include "common/stats/stats_impl.h"

#include "extensions/filters/http/buffer/buffer_filter.h"
#include "extensions/filters/http/well_known_names.h"

#include "test/mocks/buffer/mocks.h"
#include "test/mocks/http/mocks.h"
#include "test/test_common/printers.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::DoAll;
using testing::InSequence;
using testing::NiceMock;
using testing::Return;
using testing::SaveArg;
using testing::_;

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace BufferFilter {

class BufferFilterTest : public testing::Test {
public:
  BufferFilterTest()
      : config_{new BufferFilterConfig{BufferFilter::generateStats("", store_), 1024 * 1024,
                                       std::chrono::seconds(0)}},
        filter_(config_) {
    filter_.setDecoderFilterCallbacks(callbacks_);
  }

  void expectTimerCreate() { timer_ = new NiceMock<Event::MockTimer>(&callbacks_.dispatcher_); }

  void routeLocalConfig(const envoy::config::filter::http::buffer::v2::BufferPerRoute* route_cfg,
                        const envoy::config::filter::http::buffer::v2::BufferPerRoute* vhost_cfg) {

    ON_CALL(callbacks_.route_->route_entry_, perFilterConfig(HttpFilterNames::get().BUFFER))
        .WillByDefault(Return(route_cfg));
    ON_CALL(callbacks_.route_->route_entry_.virtual_host_,
            perFilterConfig(HttpFilterNames::get().BUFFER))
        .WillByDefault(Return(vhost_cfg));
  }

  NiceMock<Http::MockStreamDecoderFilterCallbacks> callbacks_;
  Stats::IsolatedStoreImpl store_;
  std::shared_ptr<BufferFilterConfig> config_;
  BufferFilter filter_;
  Event::MockTimer* timer_{};
};

TEST_F(BufferFilterTest, HeaderOnlyRequest) {
  Http::TestHeaderMapImpl headers;
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_.decodeHeaders(headers, true));
}

TEST_F(BufferFilterTest, RequestWithData) {
  InSequence s;

  expectTimerCreate();

  Http::TestHeaderMapImpl headers;
  EXPECT_EQ(Http::FilterHeadersStatus::StopIteration, filter_.decodeHeaders(headers, false));

  Buffer::OwnedImpl data1("hello");
  EXPECT_EQ(Http::FilterDataStatus::StopIterationAndBuffer, filter_.decodeData(data1, false));

  Buffer::OwnedImpl data2(" world");
  EXPECT_EQ(Http::FilterDataStatus::Continue, filter_.decodeData(data2, true));
}

TEST_F(BufferFilterTest, RequestTimeout) {
  InSequence s;

  expectTimerCreate();

  Http::TestHeaderMapImpl headers;
  EXPECT_EQ(Http::FilterHeadersStatus::StopIteration, filter_.decodeHeaders(headers, false));

  Http::TestHeaderMapImpl response_headers{
      {":status", "408"}, {"content-length", "22"}, {"content-type", "text/plain"}};
  EXPECT_CALL(callbacks_, encodeHeaders_(HeaderMapEqualRef(&response_headers), false));
  EXPECT_CALL(callbacks_, encodeData(_, true));
  timer_->callback_();

  filter_.onDestroy();
  EXPECT_EQ(1U, config_->stats_.rq_timeout_.value());
}

TEST_F(BufferFilterTest, TxResetAfterEndStream) {
  InSequence s;

  expectTimerCreate();

  Http::TestHeaderMapImpl headers;
  EXPECT_EQ(Http::FilterHeadersStatus::StopIteration, filter_.decodeHeaders(headers, false));

  Buffer::OwnedImpl data1("hello");
  EXPECT_EQ(Http::FilterDataStatus::StopIterationAndBuffer, filter_.decodeData(data1, false));

  Buffer::OwnedImpl data2(" world");
  EXPECT_EQ(Http::FilterDataStatus::Continue, filter_.decodeData(data2, true));

  // It's possible that the stream will be reset on the TX side even after RX end stream. Mimic
  // that here.
  filter_.onDestroy();
}

TEST_F(BufferFilterTest, RouteConfigOverride) {
  envoy::config::filter::http::buffer::v2::BufferPerRoute route_cfg;
  auto* buf = route_cfg.mutable_buffer();
  buf->mutable_max_request_bytes()->set_value(123);
  buf->mutable_max_request_time()->set_seconds(456);
  envoy::config::filter::http::buffer::v2::BufferPerRoute vhost_cfg;
  vhost_cfg.set_disabled(true);
  routeLocalConfig(&route_cfg, &vhost_cfg);

  EXPECT_CALL(callbacks_, setDecoderBufferLimit(123ULL));
  expectTimerCreate();

  Http::TestHeaderMapImpl headers;
  EXPECT_EQ(Http::FilterHeadersStatus::StopIteration, filter_.decodeHeaders(headers, false));

  filter_.onDestroy();
}

TEST_F(BufferFilterTest, VHostConfigOverride) {
  envoy::config::filter::http::buffer::v2::BufferPerRoute vhost_cfg;
  auto* buf = vhost_cfg.mutable_buffer();
  buf->mutable_max_request_bytes()->set_value(789);
  buf->mutable_max_request_time()->set_seconds(1011);
  routeLocalConfig(nullptr, &vhost_cfg);

  EXPECT_CALL(callbacks_, setDecoderBufferLimit(789ULL));
  expectTimerCreate();

  Http::TestHeaderMapImpl headers;
  EXPECT_EQ(Http::FilterHeadersStatus::StopIteration, filter_.decodeHeaders(headers, false));
  filter_.onDestroy();
}

TEST_F(BufferFilterTest, RouteDisabledConfigOverride) {
  envoy::config::filter::http::buffer::v2::BufferPerRoute vhost_cfg;
  vhost_cfg.set_disabled(true);
  routeLocalConfig(nullptr, &vhost_cfg);

  Http::TestHeaderMapImpl headers;
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_.decodeHeaders(headers, false));
}

} // namespace BufferFilter
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
