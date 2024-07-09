#include <memory>

#include "source/common/tracing/common_values.h"
#include "source/extensions/filters/network/generic_proxy/router/router.h"

#include "test/extensions/filters/network/generic_proxy/fake_codec.h"
#include "test/extensions/filters/network/generic_proxy/mocks/codec.h"
#include "test/extensions/filters/network/generic_proxy/mocks/filter.h"
#include "test/extensions/filters/network/generic_proxy/mocks/route.h"
#include "test/mocks/server/factory_context.h"
#include "test/test_common/registry.h"
#include "test/test_common/utility.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::NiceMock;
using testing::Return;

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace GenericProxy {
namespace Router {
namespace {

class MockUpstreamRequestCallbacks : public UpstreamRequestCallbacks {
public:
  MockUpstreamRequestCallbacks() {
    ON_CALL(*this, onUpstreamFailure(_, _)).WillByDefault(testing::Invoke([&](auto, auto) {
      if (upstream_ != nullptr) {
        upstream_->removeUpstreamRequest(1);
        upstream_->cleanUp(true);
      }
    }));
    ON_CALL(*this, onConnectionClose(_)).WillByDefault(testing::Invoke([&](auto) {
      if (upstream_ != nullptr) {
        upstream_->removeUpstreamRequest(1);
        upstream_->cleanUp(true);
      }
    }));
    ON_CALL(*this, onDecodingSuccess(_, _))
        .WillByDefault(testing::Invoke([&](ResponseHeaderFramePtr frame, auto) {
          if (upstream_ != nullptr) {
            if (frame->frameFlags().endStream()) {
              upstream_->removeUpstreamRequest(frame->frameFlags().streamId());
              upstream_->cleanUp(false);
            }
          }
        }));
    ON_CALL(*this, onDecodingSuccess(_))
        .WillByDefault(testing::Invoke([&](ResponseCommonFramePtr frame) {
          if (upstream_ != nullptr) {
            if (frame->frameFlags().endStream()) {
              upstream_->removeUpstreamRequest(frame->frameFlags().streamId());
              upstream_->cleanUp(false);
            }
          }
        }));
    ON_CALL(*this, onDecodingFailure(_)).WillByDefault(testing::Invoke([&](auto) {
      if (upstream_ != nullptr) {
        upstream_->removeUpstreamRequest(1);
        upstream_->cleanUp(true);
      }
    }));
  }

  MOCK_METHOD(void, onUpstreamFailure,
              (ConnectionPool::PoolFailureReason reason,
               absl::string_view transport_failure_reason));
  MOCK_METHOD(void, onUpstreamSuccess, ());

  MOCK_METHOD(void, onConnectionClose, (Network::ConnectionEvent event));
  MOCK_METHOD(void, onDecodingSuccess,
              (ResponseHeaderFramePtr response_header_frame, absl::optional<StartTime> start_time));
  MOCK_METHOD(void, onDecodingSuccess, (ResponseCommonFramePtr response_common_frame));
  MOCK_METHOD(void, onDecodingFailure, (absl::string_view reason));

  GenericUpstream* upstream_{};
};

TEST(SenseLessTest, SenseLessTest) {
  UniqueRequestManager request_manager;
  MockUpstreamRequestCallbacks mock_upstream_request_callbacks;

  EXPECT_FALSE(request_manager.contains(0));
  request_manager.appendUpstreamRequest(0, &mock_upstream_request_callbacks);
  EXPECT_TRUE(request_manager.contains(0));

  EventWatcher watcher(nullptr);
  watcher.onAboveWriteBufferHighWatermark();
  watcher.onBelowWriteBufferLowWatermark();
}

class UpstreamTest : public testing::Test {
public:
  UpstreamTest() {
    mock_downstream_connection_1_.stream_info_.filter_state_ =
        std::make_shared<StreamInfo::FilterStateImpl>(
            StreamInfo::FilterState::LifeSpan::Connection);
    mock_downstream_connection_2_.stream_info_.filter_state_ =
        std::make_shared<StreamInfo::FilterStateImpl>(
            StreamInfo::FilterState::LifeSpan::Connection);

    // This should be called only once for one case.
    ON_CALL(mock_codec_factory_, createClientCodec())
        .WillByDefault(Return(ByMove(std::move(mock_client_codec_))));

    ON_CALL(*mock_client_codec_raw_, setCodecCallbacks(_))
        .WillByDefault(testing::Invoke(
            [&](ClientCodecCallbacks& callbacks) { cocec_callbacks_ = &callbacks; }));
  }

  std::shared_ptr<BoundGenericUpstream> createBoundGenericUpstream(size_t connection_id = 1) {
    if (connection_id == 1) {
      auto result = DefaultGenericUpstreamFactory::get().createGenericUpstream(
          thread_local_cluster_, nullptr, mock_downstream_connection_1_, mock_codec_factory_, true);
      return std::dynamic_pointer_cast<BoundGenericUpstream>(result);
    } else {
      auto result = DefaultGenericUpstreamFactory::get().createGenericUpstream(
          thread_local_cluster_, nullptr, mock_downstream_connection_2_, mock_codec_factory_, true);
      return std::dynamic_pointer_cast<BoundGenericUpstream>(result);
    }
  }
  std::shared_ptr<OwnedGenericUpstream> createOwnedGenericUpstream() {
    auto result = DefaultGenericUpstreamFactory::get().createGenericUpstream(
        thread_local_cluster_, nullptr, mock_downstream_connection_1_, mock_codec_factory_, false);
    return std::dynamic_pointer_cast<OwnedGenericUpstream>(result);
  }

  NiceMock<Upstream::MockThreadLocalCluster> thread_local_cluster_;
  NiceMock<MockCodecFactory> mock_codec_factory_;
  std::unique_ptr<NiceMock<MockClientCodec>> mock_client_codec_{
      std::make_unique<NiceMock<MockClientCodec>>()};
  NiceMock<MockClientCodec>* mock_client_codec_raw_{mock_client_codec_.get()};
  ClientCodecCallbacks* cocec_callbacks_{};

  NiceMock<Network::MockClientConnection> mock_upstream_connection_;

  NiceMock<Network::MockServerConnection> mock_downstream_connection_1_;
  NiceMock<Network::MockServerConnection> mock_downstream_connection_2_;
};

TEST_F(UpstreamTest, BoundGenericUpstreamWillBeReusedForSameConnection) {
  EXPECT_CALL(thread_local_cluster_, tcpConnPool(_, _));
  auto generic_upstream1 = createBoundGenericUpstream(1);
  auto generic_upstream2 = createBoundGenericUpstream(1);
  EXPECT_CALL(thread_local_cluster_, tcpConnPool(_, _));
  auto generic_upstream3 = createBoundGenericUpstream(2);

  EXPECT_EQ(generic_upstream1, generic_upstream2);
  EXPECT_NE(generic_upstream1, generic_upstream3);
}

TEST_F(UpstreamTest, OwnedGenericUpstreamWillNotBeReused) {
  EXPECT_CALL(thread_local_cluster_, tcpConnPool(_, _));
  auto owned_upstream1 = createOwnedGenericUpstream();

  EXPECT_CALL(thread_local_cluster_, tcpConnPool(_, _));
  auto owned_upstream2 = createOwnedGenericUpstream();

  EXPECT_NE(owned_upstream1, owned_upstream2);
}

TEST_F(UpstreamTest, NoHealthyUpstream) {
  EXPECT_CALL(thread_local_cluster_, tcpConnPool(_, _)).WillOnce(Return(absl::nullopt));
  auto generic_upstream = createBoundGenericUpstream();
  EXPECT_EQ(nullptr, generic_upstream);

  EXPECT_CALL(thread_local_cluster_, tcpConnPool(_, _)).WillOnce(Return(absl::nullopt));
  auto owned_upstream = createOwnedGenericUpstream();
  EXPECT_EQ(nullptr, owned_upstream);
}

TEST_F(UpstreamTest, BoundGenericUpstreamInitializeAndPoolReady) {
  NiceMock<MockUpstreamRequestCallbacks> mock_upstream_request_callbacks_1;
  NiceMock<MockUpstreamRequestCallbacks> mock_upstream_request_callbacks_2;
  NiceMock<MockUpstreamRequestCallbacks> mock_upstream_request_callbacks_3;

  EXPECT_CALL(thread_local_cluster_, tcpConnPool(_, _));
  auto generic_upstream = createBoundGenericUpstream();

  EXPECT_CALL(thread_local_cluster_.tcp_conn_pool_, newConnection(_));

  generic_upstream->appendUpstreamRequest(1, &mock_upstream_request_callbacks_1);
  generic_upstream->appendUpstreamRequest(3, &mock_upstream_request_callbacks_3);
  generic_upstream->appendUpstreamRequest(2, &mock_upstream_request_callbacks_2);

  // Mock pool ready.
  // Ensure the following calls are in order.
  testing::InSequence sequence;
  EXPECT_CALL(mock_upstream_request_callbacks_1, onUpstreamSuccess());
  EXPECT_CALL(mock_upstream_request_callbacks_3, onUpstreamSuccess());
  EXPECT_CALL(mock_upstream_request_callbacks_2, onUpstreamSuccess());

  thread_local_cluster_.tcp_conn_pool_.poolReady(mock_upstream_connection_);

  // Simple test for the following functions.
  generic_upstream->onAboveWriteBufferHighWatermark();
  generic_upstream->onBelowWriteBufferLowWatermark();
  generic_upstream->upstreamHost();
  generic_upstream->clientCodec();
  generic_upstream->upstreamConnection();
}

class DownstreamEventHelper : public Network::ConnectionCallbacks {
public:
  void onEvent(Network::ConnectionEvent event) override {
    if (event == Network::ConnectionEvent::LocalClose ||
        event == Network::ConnectionEvent::RemoteClose) {
      if (generic_upstream_ != nullptr) {
        for (auto stream_id : stream_ids_) {
          generic_upstream_->removeUpstreamRequest(stream_id);
          generic_upstream_->cleanUp(true);
        }
      }
    }
  }
  void onAboveWriteBufferHighWatermark() override {}
  void onBelowWriteBufferLowWatermark() override {}

  void setUpstreamAndStreams(GenericUpstream* generic_upstream,
                             const std::vector<uint64_t>& stream_ids) {
    stream_ids_ = stream_ids;
    generic_upstream_ = generic_upstream;
  }

private:
  GenericUpstream* generic_upstream_{};
  std::vector<uint64_t> stream_ids_;
};

TEST_F(UpstreamTest, BoundGenericUpstreamRepeatedRequestsThatAreWaitingUpstream) {
  // This is used to mock the downstream connection close. All pending request will be
  // reset.
  DownstreamEventHelper downstream_event_helper;
  mock_downstream_connection_1_.addConnectionCallbacks(downstream_event_helper);

  EXPECT_CALL(thread_local_cluster_, tcpConnPool(_, _));
  auto generic_upstream = createBoundGenericUpstream();
  downstream_event_helper.setUpstreamAndStreams(generic_upstream.get(), {1, 1});

  NiceMock<MockUpstreamRequestCallbacks> mock_upstream_request_callbacks_1;
  NiceMock<MockUpstreamRequestCallbacks> mock_upstream_request_callbacks_2;
  // Only set one to ensure that even the the upstream request callbacks do not
  // clean up the upstream, the upstream will do it itself.
  mock_upstream_request_callbacks_1.upstream_ = generic_upstream.get();

  EXPECT_CALL(thread_local_cluster_.tcp_conn_pool_, newConnection(_));

  generic_upstream->appendUpstreamRequest(1, &mock_upstream_request_callbacks_1);
  EXPECT_EQ(1, generic_upstream->waitingUpstreamRequestsSize());
  EXPECT_EQ(0, generic_upstream->waitingResponseRequestsSize());

  EXPECT_CALL(*thread_local_cluster_.tcp_conn_pool_.handles_.begin(), cancel(_));

  // First, the downstream connection will be closed.
  // Second, the downstream closing event will result in all pending requests reset.
  // Third, the reset and the downstream closing event will result in the pending upstream
  // connecting be cancelled.

  EXPECT_CALL(mock_downstream_connection_1_, close(_));

  // Same stream id with first request.
  generic_upstream->appendUpstreamRequest(1, &mock_upstream_request_callbacks_2);

  EXPECT_EQ(0, generic_upstream->waitingUpstreamRequestsSize());
  EXPECT_EQ(0, generic_upstream->waitingResponseRequestsSize());
}

TEST_F(UpstreamTest, BoundGenericUpstreamRepeatedRequestsThatAreWaitingResponse) {
  DownstreamEventHelper downstream_event_helper;
  mock_downstream_connection_1_.addConnectionCallbacks(downstream_event_helper);

  EXPECT_CALL(thread_local_cluster_, tcpConnPool(_, _));
  auto generic_upstream = createBoundGenericUpstream();
  downstream_event_helper.setUpstreamAndStreams(generic_upstream.get(), {1, 2, 2});

  NiceMock<MockUpstreamRequestCallbacks> mock_upstream_request_callbacks_1;
  NiceMock<MockUpstreamRequestCallbacks> mock_upstream_request_callbacks_2;
  NiceMock<MockUpstreamRequestCallbacks> mock_upstream_request_callbacks_3;
  // Only set one to ensure that even the the upstream request callbacks do not
  // clean up the upstream, the upstream will do it itself.
  mock_upstream_request_callbacks_1.upstream_ = generic_upstream.get();

  EXPECT_CALL(thread_local_cluster_.tcp_conn_pool_, newConnection(_));

  generic_upstream->appendUpstreamRequest(1, &mock_upstream_request_callbacks_1);
  generic_upstream->appendUpstreamRequest(2, &mock_upstream_request_callbacks_2);

  EXPECT_EQ(2, generic_upstream->waitingUpstreamRequestsSize());
  EXPECT_EQ(0, generic_upstream->waitingResponseRequestsSize());

  EXPECT_CALL(mock_upstream_request_callbacks_1, onUpstreamSuccess());
  EXPECT_CALL(mock_upstream_request_callbacks_2, onUpstreamSuccess());

  EXPECT_CALL(*thread_local_cluster_.tcp_conn_pool_.connection_data_, addUpstreamCallbacks(_))
      .WillOnce(testing::Invoke([&](Tcp::ConnectionPool::UpstreamCallbacks& cb) {
        mock_upstream_connection_.addConnectionCallbacks(cb);
      }));
  thread_local_cluster_.tcp_conn_pool_.poolReady(mock_upstream_connection_);

  EXPECT_EQ(0, generic_upstream->waitingUpstreamRequestsSize());
  EXPECT_EQ(2, generic_upstream->waitingResponseRequestsSize());

  // First, the downstream connection will be closed.
  // Second, the downstream closing event will result in all pending requests reset.
  // Third, the first request reset will result in the upstream connection be closed.
  // Fourth, the upstream closing event will result in the downstream connection be closed again.
  testing::InSequence sequence;
  EXPECT_CALL(mock_downstream_connection_1_, close(_));
  EXPECT_CALL(mock_upstream_connection_, close(_));
  EXPECT_CALL(mock_upstream_request_callbacks_2, onConnectionClose(_));
  EXPECT_CALL(mock_downstream_connection_1_, close(_));

  // Same stream id with first request.
  generic_upstream->appendUpstreamRequest(2, &mock_upstream_request_callbacks_3);

  EXPECT_EQ(0, generic_upstream->waitingUpstreamRequestsSize());
  EXPECT_EQ(0, generic_upstream->waitingResponseRequestsSize());
}

TEST_F(UpstreamTest, BoundGenericUpstreamOnPoolFailure) {
  DownstreamEventHelper downstream_event_helper;
  mock_downstream_connection_1_.addConnectionCallbacks(downstream_event_helper);

  EXPECT_CALL(thread_local_cluster_, tcpConnPool(_, _));
  auto generic_upstream = createBoundGenericUpstream();
  downstream_event_helper.setUpstreamAndStreams(generic_upstream.get(), {});

  NiceMock<MockUpstreamRequestCallbacks> mock_upstream_request_callbacks_1;
  NiceMock<MockUpstreamRequestCallbacks> mock_upstream_request_callbacks_2;
  NiceMock<MockUpstreamRequestCallbacks> mock_upstream_request_callbacks_3;
  // Only set one to ensure that even the the upstream request callbacks do not
  // clean up the upstream, the upstream will do it itself.
  mock_upstream_request_callbacks_1.upstream_ = generic_upstream.get();

  EXPECT_CALL(thread_local_cluster_.tcp_conn_pool_, newConnection(_));

  generic_upstream->appendUpstreamRequest(1, &mock_upstream_request_callbacks_1);
  generic_upstream->appendUpstreamRequest(2, &mock_upstream_request_callbacks_2);
  generic_upstream->appendUpstreamRequest(3, &mock_upstream_request_callbacks_3);

  EXPECT_EQ(3, generic_upstream->waitingUpstreamRequestsSize());
  EXPECT_EQ(0, generic_upstream->waitingResponseRequestsSize());

  testing::InSequence sequence;
  EXPECT_CALL(mock_upstream_request_callbacks_1, onUpstreamFailure(_, _));
  EXPECT_CALL(mock_upstream_request_callbacks_2, onUpstreamFailure(_, _));
  EXPECT_CALL(mock_upstream_request_callbacks_3, onUpstreamFailure(_, _));

  EXPECT_CALL(mock_downstream_connection_1_, close(_));

  thread_local_cluster_.tcp_conn_pool_.poolFailure(ConnectionPool::PoolFailureReason::Timeout);

  EXPECT_EQ(0, generic_upstream->waitingUpstreamRequestsSize());
  EXPECT_EQ(0, generic_upstream->waitingResponseRequestsSize());

  // Try append again but make no sense.
  generic_upstream->appendUpstreamRequest(1, &mock_upstream_request_callbacks_1);
  EXPECT_EQ(0, generic_upstream->waitingUpstreamRequestsSize());
  EXPECT_EQ(0, generic_upstream->waitingResponseRequestsSize());
}

TEST_F(UpstreamTest, BoundGenericUpstreamDecodingSuccess) {

  DownstreamEventHelper downstream_event_helper;
  mock_downstream_connection_1_.addConnectionCallbacks(downstream_event_helper);

  EXPECT_CALL(thread_local_cluster_, tcpConnPool(_, _));
  auto generic_upstream = createBoundGenericUpstream();
  downstream_event_helper.setUpstreamAndStreams(generic_upstream.get(), {});

  NiceMock<MockUpstreamRequestCallbacks> mock_upstream_request_callbacks_1;
  NiceMock<MockUpstreamRequestCallbacks> mock_upstream_request_callbacks_2;
  // Only set one to ensure that even the the upstream request callbacks do not
  // clean up the upstream, the upstream will do it itself.
  mock_upstream_request_callbacks_1.upstream_ = generic_upstream.get();

  EXPECT_CALL(thread_local_cluster_.tcp_conn_pool_, newConnection(_));

  generic_upstream->appendUpstreamRequest(1, &mock_upstream_request_callbacks_1);
  generic_upstream->appendUpstreamRequest(2, &mock_upstream_request_callbacks_2);

  EXPECT_EQ(2, generic_upstream->waitingUpstreamRequestsSize());
  EXPECT_EQ(0, generic_upstream->waitingResponseRequestsSize());

  EXPECT_CALL(*thread_local_cluster_.tcp_conn_pool_.connection_data_, addUpstreamCallbacks(_))
      .WillOnce(testing::Invoke([&](Tcp::ConnectionPool::UpstreamCallbacks& cb) {
        mock_upstream_connection_.addConnectionCallbacks(cb);
      }));
  thread_local_cluster_.tcp_conn_pool_.poolReady(mock_upstream_connection_);

  EXPECT_EQ(0, generic_upstream->waitingUpstreamRequestsSize());
  EXPECT_EQ(2, generic_upstream->waitingResponseRequestsSize());

  auto response_1 = std::make_unique<FakeStreamCodecFactory::FakeResponse>();
  response_1->stream_frame_flags_ = FrameFlags(1);

  auto response_2 = std::make_unique<FakeStreamCodecFactory::FakeResponse>();
  response_2->stream_frame_flags_ = FrameFlags(2);

  auto unknown_response = std::make_unique<FakeStreamCodecFactory::FakeResponse>();
  unknown_response->stream_frame_flags_ = FrameFlags(3);

  EXPECT_CALL(*mock_client_codec_raw_, decode(_, _))
      .WillOnce(testing::Invoke([&](Buffer::Instance&, bool) {
        // Will be ignored.
        cocec_callbacks_->onDecodingSuccess(std::move(unknown_response), {});

        EXPECT_CALL(mock_upstream_request_callbacks_2, onDecodingSuccess(_, _));
        cocec_callbacks_->onDecodingSuccess(std::move(response_2), {});

        EXPECT_CALL(mock_upstream_request_callbacks_1, onDecodingSuccess(_, _));
        cocec_callbacks_->onDecodingSuccess(std::move(response_1), {});
      }));

  Buffer::OwnedImpl fake_buffer;
  fake_buffer.add("fake data");
  generic_upstream->onUpstreamData(fake_buffer, false);

  EXPECT_EQ(0, generic_upstream->waitingUpstreamRequestsSize());
  EXPECT_EQ(0, generic_upstream->waitingResponseRequestsSize());
}

TEST_F(UpstreamTest, BoundGenericUpstreamDecodingSuccessWithMultipleFrames) {
  DownstreamEventHelper downstream_event_helper;
  mock_downstream_connection_1_.addConnectionCallbacks(downstream_event_helper);

  EXPECT_CALL(thread_local_cluster_, tcpConnPool(_, _));
  auto generic_upstream = createBoundGenericUpstream();
  downstream_event_helper.setUpstreamAndStreams(generic_upstream.get(), {});

  NiceMock<MockUpstreamRequestCallbacks> mock_upstream_request_callbacks_1;
  NiceMock<MockUpstreamRequestCallbacks> mock_upstream_request_callbacks_2;
  // Only set one to ensure that even the the upstream request callbacks do not
  // clean up the upstream, the upstream will do it itself.
  mock_upstream_request_callbacks_1.upstream_ = generic_upstream.get();

  EXPECT_CALL(thread_local_cluster_.tcp_conn_pool_, newConnection(_));

  generic_upstream->appendUpstreamRequest(1, &mock_upstream_request_callbacks_1);
  generic_upstream->appendUpstreamRequest(2, &mock_upstream_request_callbacks_2);

  EXPECT_EQ(2, generic_upstream->waitingUpstreamRequestsSize());
  EXPECT_EQ(0, generic_upstream->waitingResponseRequestsSize());

  EXPECT_CALL(*thread_local_cluster_.tcp_conn_pool_.connection_data_, addUpstreamCallbacks(_))
      .WillOnce(testing::Invoke([&](Tcp::ConnectionPool::UpstreamCallbacks& cb) {
        mock_upstream_connection_.addConnectionCallbacks(cb);
      }));
  thread_local_cluster_.tcp_conn_pool_.poolReady(mock_upstream_connection_);

  EXPECT_EQ(0, generic_upstream->waitingUpstreamRequestsSize());
  EXPECT_EQ(2, generic_upstream->waitingResponseRequestsSize());

  auto response_1 = std::make_unique<FakeStreamCodecFactory::FakeResponse>();
  response_1->stream_frame_flags_ = FrameFlags(1, FrameFlags::FLAG_EMPTY);

  auto response_2 = std::make_unique<FakeStreamCodecFactory::FakeResponse>();
  response_2->stream_frame_flags_ = FrameFlags(2, FrameFlags::FLAG_EMPTY);

  auto unknown_response = std::make_unique<FakeStreamCodecFactory::FakeResponse>();
  unknown_response->stream_frame_flags_ = FrameFlags(3);

  EXPECT_CALL(*mock_client_codec_raw_, decode(_, _))
      .WillOnce(testing::Invoke([&](Buffer::Instance&, bool) {
        // Will be ignored.
        cocec_callbacks_->onDecodingSuccess(std::move(unknown_response), {});

        EXPECT_CALL(mock_upstream_request_callbacks_1, onDecodingSuccess(_, _));
        cocec_callbacks_->onDecodingSuccess(std::move(response_1), {});

        EXPECT_CALL(mock_upstream_request_callbacks_2, onDecodingSuccess(_, _));
        cocec_callbacks_->onDecodingSuccess(std::move(response_2), {});
      }));

  Buffer::OwnedImpl fake_buffer;
  fake_buffer.add("fake data");
  generic_upstream->onUpstreamData(fake_buffer, false);

  EXPECT_EQ(0, generic_upstream->waitingUpstreamRequestsSize());
  EXPECT_EQ(2, generic_upstream->waitingResponseRequestsSize());

  auto response_1_frame = std::make_unique<FakeStreamCodecFactory::FakeCommonFrame>();
  response_1_frame->stream_frame_flags_ = FrameFlags(1);

  auto response_2_frame = std::make_unique<FakeStreamCodecFactory::FakeCommonFrame>();
  response_2_frame->stream_frame_flags_ = FrameFlags(2);

  auto unknown_response_frame = std::make_unique<FakeStreamCodecFactory::FakeCommonFrame>();
  unknown_response_frame->stream_frame_flags_ = FrameFlags(3);

  EXPECT_CALL(*mock_client_codec_raw_, decode(_, _))
      .WillOnce(testing::Invoke([&](Buffer::Instance&, bool) {
        // Will be ignored.
        cocec_callbacks_->onDecodingSuccess(std::move(unknown_response_frame));

        EXPECT_CALL(mock_upstream_connection_, close(_)).Times(0);
        EXPECT_CALL(mock_upstream_request_callbacks_2, onDecodingSuccess(_));
        cocec_callbacks_->onDecodingSuccess(std::move(response_2_frame));

        EXPECT_CALL(mock_upstream_connection_, close(_)).Times(0);
        EXPECT_CALL(mock_upstream_request_callbacks_1, onDecodingSuccess(_));
        cocec_callbacks_->onDecodingSuccess(std::move(response_1_frame));
      }));

  generic_upstream->onUpstreamData(fake_buffer, false);

  EXPECT_EQ(0, generic_upstream->waitingUpstreamRequestsSize());
  EXPECT_EQ(0, generic_upstream->waitingResponseRequestsSize());
}

TEST_F(UpstreamTest, BoundGenericUpstreamDecodingFailureAndNoUpstreamConnectionClose) {
  DownstreamEventHelper downstream_event_helper;
  mock_downstream_connection_1_.addConnectionCallbacks(downstream_event_helper);

  EXPECT_CALL(thread_local_cluster_, tcpConnPool(_, _));
  auto generic_upstream = createBoundGenericUpstream();
  downstream_event_helper.setUpstreamAndStreams(generic_upstream.get(), {});

  NiceMock<MockUpstreamRequestCallbacks> mock_upstream_request_callbacks_1;
  NiceMock<MockUpstreamRequestCallbacks> mock_upstream_request_callbacks_2;

  EXPECT_CALL(thread_local_cluster_.tcp_conn_pool_, newConnection(_));

  generic_upstream->appendUpstreamRequest(1, &mock_upstream_request_callbacks_1);
  generic_upstream->appendUpstreamRequest(2, &mock_upstream_request_callbacks_2);

  EXPECT_EQ(2, generic_upstream->waitingUpstreamRequestsSize());
  EXPECT_EQ(0, generic_upstream->waitingResponseRequestsSize());

  EXPECT_CALL(*thread_local_cluster_.tcp_conn_pool_.connection_data_, addUpstreamCallbacks(_))
      .WillOnce(testing::Invoke([&](Tcp::ConnectionPool::UpstreamCallbacks& cb) {
        mock_upstream_connection_.addConnectionCallbacks(cb);
      }));
  thread_local_cluster_.tcp_conn_pool_.poolReady(mock_upstream_connection_);

  EXPECT_EQ(0, generic_upstream->waitingUpstreamRequestsSize());
  EXPECT_EQ(2, generic_upstream->waitingResponseRequestsSize());

  auto response_1 = std::make_unique<FakeStreamCodecFactory::FakeResponse>();
  response_1->stream_frame_flags_ = FrameFlags(1);

  auto response_2 = std::make_unique<FakeStreamCodecFactory::FakeResponse>();
  response_2->stream_frame_flags_ = FrameFlags(2);

  EXPECT_CALL(*mock_client_codec_raw_, decode(_, _))
      .WillOnce(testing::Invoke([&](Buffer::Instance&, bool) {
        // Upstream is not set into the mock upstream request callbacks. So the
        // onDecodingFailure() will not clean up the upstream and will not result
        // in the upstream connection close.
        EXPECT_CALL(mock_upstream_request_callbacks_1, onDecodingFailure(_));
        EXPECT_CALL(mock_upstream_request_callbacks_2, onDecodingFailure(_));

        cocec_callbacks_->onDecodingFailure("test");
      }));

  Buffer::OwnedImpl fake_buffer;
  fake_buffer.add("fake data");
  generic_upstream->onUpstreamData(fake_buffer, false);

  EXPECT_EQ(0, generic_upstream->waitingUpstreamRequestsSize());
  EXPECT_EQ(0, generic_upstream->waitingResponseRequestsSize());
}

TEST_F(UpstreamTest, BoundGenericUpstreamDecodingFailure) {
  DownstreamEventHelper downstream_event_helper;
  mock_downstream_connection_1_.addConnectionCallbacks(downstream_event_helper);

  EXPECT_CALL(thread_local_cluster_, tcpConnPool(_, _));
  auto generic_upstream = createBoundGenericUpstream();
  downstream_event_helper.setUpstreamAndStreams(generic_upstream.get(), {});

  NiceMock<MockUpstreamRequestCallbacks> mock_upstream_request_callbacks_1;
  NiceMock<MockUpstreamRequestCallbacks> mock_upstream_request_callbacks_2;

  EXPECT_CALL(thread_local_cluster_.tcp_conn_pool_, newConnection(_));

  generic_upstream->appendUpstreamRequest(1, &mock_upstream_request_callbacks_1);
  generic_upstream->appendUpstreamRequest(2, &mock_upstream_request_callbacks_2);

  EXPECT_EQ(2, generic_upstream->waitingUpstreamRequestsSize());
  EXPECT_EQ(0, generic_upstream->waitingResponseRequestsSize());

  EXPECT_CALL(*thread_local_cluster_.tcp_conn_pool_.connection_data_, addUpstreamCallbacks(_))
      .WillOnce(testing::Invoke([&](Tcp::ConnectionPool::UpstreamCallbacks& cb) {
        mock_upstream_connection_.addConnectionCallbacks(cb);
      }));
  thread_local_cluster_.tcp_conn_pool_.poolReady(mock_upstream_connection_);

  EXPECT_EQ(0, generic_upstream->waitingUpstreamRequestsSize());
  EXPECT_EQ(2, generic_upstream->waitingResponseRequestsSize());

  auto response_1 = std::make_unique<FakeStreamCodecFactory::FakeResponse>();
  response_1->stream_frame_flags_ = FrameFlags(1);

  auto response_2 = std::make_unique<FakeStreamCodecFactory::FakeResponse>();
  response_2->stream_frame_flags_ = FrameFlags(2);

  EXPECT_CALL(*mock_client_codec_raw_, decode(_, _))
      .WillOnce(testing::Invoke([&](Buffer::Instance&, bool) {
        EXPECT_EQ(cocec_callbacks_->connection().ptr(), &mock_upstream_connection_);
        EXPECT_EQ(cocec_callbacks_->upstreamCluster().ptr(),
                  &thread_local_cluster_.tcp_conn_pool_.host_->cluster_);

        std::vector<uint32_t> called_decoding_failure;
        std::vector<uint32_t> called_connection_close;

        EXPECT_CALL(mock_upstream_connection_, close(_));

        ON_CALL(mock_upstream_request_callbacks_1, onDecodingFailure(_))
            .WillByDefault(testing::Invoke([&](auto) {
              called_decoding_failure.push_back(1);
              generic_upstream->removeUpstreamRequest(1);
              generic_upstream->cleanUp(true);
            }));
        ON_CALL(mock_upstream_request_callbacks_1, onConnectionClose(_))
            .WillByDefault(testing::Invoke([&](auto) {
              called_connection_close.push_back(1);
              generic_upstream->removeUpstreamRequest(1);
              // The onConnectionClose() is called when the upstream connection is closed.
              // When the upstream connection is closed because the onDecodingFailure(),
              // the upstream was already cleaned up. So, the cleanUp() here do anything.
              generic_upstream->cleanUp(true);
            }));
        ON_CALL(mock_upstream_request_callbacks_2, onDecodingFailure(_))
            .WillByDefault(testing::Invoke([&](auto) {
              called_decoding_failure.push_back(2);
              generic_upstream->removeUpstreamRequest(2);
              generic_upstream->cleanUp(true);
            }));
        ON_CALL(mock_upstream_request_callbacks_2, onConnectionClose(_))
            .WillByDefault(testing::Invoke([&](auto) {
              called_connection_close.push_back(2);
              generic_upstream->removeUpstreamRequest(2);
              // The onConnectionClose() is called when the upstream connection is closed.
              // When the upstream connection is closed because the onDecodingFailure(),
              // the upstream was already cleaned up. So, the cleanUp() here do anything.
              generic_upstream->cleanUp(true);
            }));
        EXPECT_CALL(mock_downstream_connection_1_, close(_));

        cocec_callbacks_->onDecodingFailure("test");

        EXPECT_EQ(1, called_decoding_failure.size());
        EXPECT_EQ(1, called_connection_close.size());

        // One of callbacks' onDecodingFailure() will be called first. And the other one's
        // onConnectionClose() will be called then.
        EXPECT_TRUE((called_decoding_failure[0] == 1 && called_connection_close[0] == 2) ||
                    (called_decoding_failure[0] == 2 && called_connection_close[0] == 1));
      }));

  Buffer::OwnedImpl fake_buffer;
  fake_buffer.add("fake data");
  generic_upstream->onUpstreamData(fake_buffer, false);

  EXPECT_EQ(0, generic_upstream->waitingUpstreamRequestsSize());
  EXPECT_EQ(0, generic_upstream->waitingResponseRequestsSize());
}

TEST_F(UpstreamTest, BoundGenericUpstreamUpstreamConnectionClose) {
  DownstreamEventHelper downstream_event_helper;
  mock_downstream_connection_1_.addConnectionCallbacks(downstream_event_helper);

  EXPECT_CALL(thread_local_cluster_, tcpConnPool(_, _));
  auto generic_upstream = createBoundGenericUpstream();
  downstream_event_helper.setUpstreamAndStreams(generic_upstream.get(), {});

  NiceMock<MockUpstreamRequestCallbacks> mock_upstream_request_callbacks_1;
  NiceMock<MockUpstreamRequestCallbacks> mock_upstream_request_callbacks_2;
  // Only set one to ensure that even the the upstream request callbacks do not
  // clean up the upstream, the upstream will do it itself.
  mock_upstream_request_callbacks_1.upstream_ = generic_upstream.get();

  EXPECT_CALL(thread_local_cluster_.tcp_conn_pool_, newConnection(_));

  generic_upstream->appendUpstreamRequest(1, &mock_upstream_request_callbacks_1);
  generic_upstream->appendUpstreamRequest(2, &mock_upstream_request_callbacks_2);

  EXPECT_EQ(2, generic_upstream->waitingUpstreamRequestsSize());
  EXPECT_EQ(0, generic_upstream->waitingResponseRequestsSize());

  EXPECT_CALL(*thread_local_cluster_.tcp_conn_pool_.connection_data_, addUpstreamCallbacks(_))
      .WillOnce(testing::Invoke([&](Tcp::ConnectionPool::UpstreamCallbacks& cb) {
        mock_upstream_connection_.addConnectionCallbacks(cb);
      }));
  thread_local_cluster_.tcp_conn_pool_.poolReady(mock_upstream_connection_);

  EXPECT_EQ(0, generic_upstream->waitingUpstreamRequestsSize());
  EXPECT_EQ(2, generic_upstream->waitingResponseRequestsSize());

  EXPECT_CALL(mock_upstream_request_callbacks_1, onConnectionClose(_));
  EXPECT_CALL(mock_upstream_request_callbacks_2, onConnectionClose(_));
  EXPECT_CALL(mock_downstream_connection_1_, close(_));

  mock_upstream_connection_.close(Network::ConnectionCloseType::NoFlush);

  EXPECT_EQ(0, generic_upstream->waitingUpstreamRequestsSize());
  EXPECT_EQ(0, generic_upstream->waitingResponseRequestsSize());
}

TEST_F(UpstreamTest, OwnedGenericUpstreamInitializeAndDestroyUpstreamBeforePoolReady) {
  EXPECT_CALL(thread_local_cluster_, tcpConnPool(_, _));
  auto generic_upstream = createOwnedGenericUpstream();

  NiceMock<MockUpstreamRequestCallbacks> mock_upstream_request_callbacks_1;
  mock_upstream_request_callbacks_1.upstream_ = generic_upstream.get();

  EXPECT_CALL(thread_local_cluster_.tcp_conn_pool_, newConnection(_));

  generic_upstream->appendUpstreamRequest(1, &mock_upstream_request_callbacks_1);

  EXPECT_FALSE(generic_upstream->upstreamConnection().has_value());

  // Destroy the upstream before the pool ready and no clean up.
  generic_upstream.reset();
}

TEST_F(UpstreamTest, OwnedGenericUpstreamInitializeAndPoolReady) {
  EXPECT_CALL(thread_local_cluster_, tcpConnPool(_, _));
  auto generic_upstream = createOwnedGenericUpstream();

  NiceMock<MockUpstreamRequestCallbacks> mock_upstream_request_callbacks_1;
  mock_upstream_request_callbacks_1.upstream_ = generic_upstream.get();

  EXPECT_CALL(thread_local_cluster_.tcp_conn_pool_, newConnection(_));

  generic_upstream->appendUpstreamRequest(1, &mock_upstream_request_callbacks_1);

  // Mock pool ready.
  // Ensure the following calls are in order.
  testing::InSequence sequence;
  EXPECT_CALL(mock_upstream_request_callbacks_1, onUpstreamSuccess());

  thread_local_cluster_.tcp_conn_pool_.poolReady(mock_upstream_connection_);

  // Simple test for the following functions.
  generic_upstream->onAboveWriteBufferHighWatermark();
  generic_upstream->onBelowWriteBufferLowWatermark();
  generic_upstream->upstreamHost();
  generic_upstream->clientCodec();
  generic_upstream->upstreamConnection();
}

TEST_F(UpstreamTest, OwnedGenericUpstreamOnPoolFailure) {
  EXPECT_CALL(thread_local_cluster_, tcpConnPool(_, _));
  auto generic_upstream = createOwnedGenericUpstream();

  NiceMock<MockUpstreamRequestCallbacks> mock_upstream_request_callbacks_1;
  mock_upstream_request_callbacks_1.upstream_ = generic_upstream.get();

  EXPECT_CALL(thread_local_cluster_.tcp_conn_pool_, newConnection(_));

  generic_upstream->appendUpstreamRequest(1, &mock_upstream_request_callbacks_1);

  EXPECT_EQ(1, generic_upstream->waitingUpstreamRequestsSize());
  EXPECT_EQ(0, generic_upstream->waitingResponseRequestsSize());

  testing::InSequence sequence;
  EXPECT_CALL(mock_upstream_request_callbacks_1, onUpstreamFailure(_, _));

  thread_local_cluster_.tcp_conn_pool_.poolFailure(ConnectionPool::PoolFailureReason::Timeout);

  EXPECT_EQ(0, generic_upstream->waitingUpstreamRequestsSize());
  EXPECT_EQ(0, generic_upstream->waitingResponseRequestsSize());
}

TEST_F(UpstreamTest, OwnedGenericUpstreamDecodingSuccess) {
  EXPECT_CALL(thread_local_cluster_, tcpConnPool(_, _));
  auto generic_upstream = createOwnedGenericUpstream();

  NiceMock<MockUpstreamRequestCallbacks> mock_upstream_request_callbacks_1;
  mock_upstream_request_callbacks_1.upstream_ = generic_upstream.get();

  EXPECT_CALL(thread_local_cluster_.tcp_conn_pool_, newConnection(_));

  generic_upstream->appendUpstreamRequest(1, &mock_upstream_request_callbacks_1);

  EXPECT_EQ(1, generic_upstream->waitingUpstreamRequestsSize());

  EXPECT_CALL(*thread_local_cluster_.tcp_conn_pool_.connection_data_, addUpstreamCallbacks(_))
      .WillOnce(testing::Invoke([&](Tcp::ConnectionPool::UpstreamCallbacks& cb) {
        mock_upstream_connection_.addConnectionCallbacks(cb);
      }));
  thread_local_cluster_.tcp_conn_pool_.poolReady(mock_upstream_connection_);

  EXPECT_EQ(0, generic_upstream->waitingUpstreamRequestsSize());
  EXPECT_EQ(1, generic_upstream->waitingResponseRequestsSize());

  auto response_1 = std::make_unique<FakeStreamCodecFactory::FakeResponse>();
  response_1->stream_frame_flags_ = FrameFlags(1);

  EXPECT_CALL(*mock_client_codec_raw_, decode(_, _))
      .WillOnce(testing::Invoke([&](Buffer::Instance&, bool) {
        EXPECT_CALL(mock_upstream_request_callbacks_1, onDecodingSuccess(_, _));
        cocec_callbacks_->onDecodingSuccess(std::move(response_1), {});
      }));

  Buffer::OwnedImpl fake_buffer;
  // Empty buffer will be ignored.
  generic_upstream->onUpstreamData(fake_buffer, false);
  fake_buffer.add("fake data");
  generic_upstream->onUpstreamData(fake_buffer, false);

  EXPECT_EQ(0, generic_upstream->waitingUpstreamRequestsSize());
  EXPECT_EQ(0, generic_upstream->waitingResponseRequestsSize());
}

TEST_F(UpstreamTest, OwnedGenericUpstreamDecodingSuccessWithMultipleFrames) {
  EXPECT_CALL(thread_local_cluster_, tcpConnPool(_, _));
  auto generic_upstream = createOwnedGenericUpstream();

  NiceMock<MockUpstreamRequestCallbacks> mock_upstream_request_callbacks_1;
  mock_upstream_request_callbacks_1.upstream_ = generic_upstream.get();

  EXPECT_CALL(thread_local_cluster_.tcp_conn_pool_, newConnection(_));

  generic_upstream->appendUpstreamRequest(1, &mock_upstream_request_callbacks_1);

  EXPECT_EQ(1, generic_upstream->waitingUpstreamRequestsSize());
  EXPECT_EQ(0, generic_upstream->waitingResponseRequestsSize());

  EXPECT_CALL(*thread_local_cluster_.tcp_conn_pool_.connection_data_, addUpstreamCallbacks(_))
      .WillOnce(testing::Invoke([&](Tcp::ConnectionPool::UpstreamCallbacks& cb) {
        mock_upstream_connection_.addConnectionCallbacks(cb);
      }));
  thread_local_cluster_.tcp_conn_pool_.poolReady(mock_upstream_connection_);

  EXPECT_EQ(0, generic_upstream->waitingUpstreamRequestsSize());
  EXPECT_EQ(1, generic_upstream->waitingResponseRequestsSize());

  auto response_1 = std::make_unique<FakeStreamCodecFactory::FakeResponse>();
  response_1->stream_frame_flags_ = FrameFlags(1, FrameFlags::FLAG_EMPTY);

  EXPECT_CALL(*mock_client_codec_raw_, decode(_, _))
      .WillOnce(testing::Invoke([&](Buffer::Instance&, bool) {
        EXPECT_CALL(mock_upstream_request_callbacks_1, onDecodingSuccess(_, _));
        cocec_callbacks_->onDecodingSuccess(std::move(response_1), {});
      }));

  Buffer::OwnedImpl fake_buffer;
  // Empty buffer will be ignored.
  generic_upstream->onUpstreamData(fake_buffer, false);
  fake_buffer.add("fake data");
  generic_upstream->onUpstreamData(fake_buffer, false);

  EXPECT_EQ(0, generic_upstream->waitingUpstreamRequestsSize());
  EXPECT_EQ(1, generic_upstream->waitingResponseRequestsSize());

  auto response_1_frame = std::make_unique<FakeStreamCodecFactory::FakeCommonFrame>();
  response_1_frame->stream_frame_flags_ = FrameFlags(1);

  EXPECT_CALL(*mock_client_codec_raw_, decode(_, _))
      .WillOnce(testing::Invoke([&](Buffer::Instance&, bool) {
        EXPECT_CALL(mock_upstream_connection_, close(_)).Times(0);
        EXPECT_CALL(mock_upstream_request_callbacks_1, onDecodingSuccess(_));
        cocec_callbacks_->onDecodingSuccess(std::move(response_1_frame));
      }));

  generic_upstream->onUpstreamData(fake_buffer, false);

  EXPECT_EQ(0, generic_upstream->waitingUpstreamRequestsSize());
  EXPECT_EQ(0, generic_upstream->waitingResponseRequestsSize());
}

TEST_F(UpstreamTest, OwnedGenericUpstreamDecodingFailure) {
  EXPECT_CALL(thread_local_cluster_, tcpConnPool(_, _));
  auto generic_upstream = createOwnedGenericUpstream();

  NiceMock<MockUpstreamRequestCallbacks> mock_upstream_request_callbacks_1;
  mock_upstream_request_callbacks_1.upstream_ = generic_upstream.get();

  EXPECT_CALL(thread_local_cluster_.tcp_conn_pool_, newConnection(_));

  generic_upstream->appendUpstreamRequest(1, &mock_upstream_request_callbacks_1);

  EXPECT_EQ(1, generic_upstream->waitingUpstreamRequestsSize());
  EXPECT_EQ(0, generic_upstream->waitingResponseRequestsSize());

  EXPECT_CALL(*thread_local_cluster_.tcp_conn_pool_.connection_data_, addUpstreamCallbacks(_))
      .WillOnce(testing::Invoke([&](Tcp::ConnectionPool::UpstreamCallbacks& cb) {
        mock_upstream_connection_.addConnectionCallbacks(cb);
      }));
  thread_local_cluster_.tcp_conn_pool_.poolReady(mock_upstream_connection_);

  EXPECT_EQ(0, generic_upstream->waitingUpstreamRequestsSize());
  EXPECT_EQ(1, generic_upstream->waitingResponseRequestsSize());

  EXPECT_CALL(*mock_client_codec_raw_, decode(_, _))
      .WillOnce(testing::Invoke([&](Buffer::Instance&, bool) {
        EXPECT_CALL(mock_upstream_request_callbacks_1, onDecodingFailure(_));
        EXPECT_CALL(mock_upstream_connection_, close(_));

        cocec_callbacks_->onDecodingFailure("test");
      }));

  Buffer::OwnedImpl fake_buffer;
  fake_buffer.add("fake data");
  generic_upstream->onUpstreamData(fake_buffer, false);

  EXPECT_EQ(0, generic_upstream->waitingUpstreamRequestsSize());
  EXPECT_EQ(0, generic_upstream->waitingResponseRequestsSize());
}

TEST_F(UpstreamTest, OwnedGenericUpstreamUpstreamConnectionClose) {
  EXPECT_CALL(thread_local_cluster_, tcpConnPool(_, _));
  auto generic_upstream = createOwnedGenericUpstream();

  NiceMock<MockUpstreamRequestCallbacks> mock_upstream_request_callbacks_1;
  mock_upstream_request_callbacks_1.upstream_ = generic_upstream.get();

  EXPECT_CALL(thread_local_cluster_.tcp_conn_pool_, newConnection(_));

  generic_upstream->appendUpstreamRequest(1, &mock_upstream_request_callbacks_1);

  EXPECT_EQ(1, generic_upstream->waitingUpstreamRequestsSize());
  EXPECT_EQ(0, generic_upstream->waitingResponseRequestsSize());

  EXPECT_CALL(*thread_local_cluster_.tcp_conn_pool_.connection_data_, addUpstreamCallbacks(_))
      .WillOnce(testing::Invoke([&](Tcp::ConnectionPool::UpstreamCallbacks& cb) {
        mock_upstream_connection_.addConnectionCallbacks(cb);
      }));
  thread_local_cluster_.tcp_conn_pool_.poolReady(mock_upstream_connection_);

  EXPECT_EQ(0, generic_upstream->waitingUpstreamRequestsSize());
  EXPECT_EQ(1, generic_upstream->waitingResponseRequestsSize());

  EXPECT_CALL(mock_upstream_request_callbacks_1, onConnectionClose(_));

  mock_upstream_connection_.close(Network::ConnectionCloseType::NoFlush);

  EXPECT_EQ(0, generic_upstream->waitingUpstreamRequestsSize());
  EXPECT_EQ(0, generic_upstream->waitingResponseRequestsSize());
}

} // namespace
} // namespace Router
} // namespace GenericProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
