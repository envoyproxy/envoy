#include "test/common/upstream/health_check_fuzz_test_utils.h"

#include "test/common/upstream/utility.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace Upstream {

void HttpHealthCheckerImplTestBase::expectSessionCreate(
    const HostWithHealthCheckMap& health_check_map) {
  // Expectations are in LIFO order.
  TestSessionPtr new_test_session(new TestSession());
  test_sessions_.emplace_back(std::move(new_test_session));
  TestSession& test_session = *test_sessions_.back();
  test_session.timeout_timer_ = new NiceMock<Event::MockTimer>(&context_.dispatcher_);
  test_session.interval_timer_ = new NiceMock<Event::MockTimer>(&context_.dispatcher_);
  expectClientCreate(test_sessions_.size() - 1, health_check_map);
}

void HttpHealthCheckerImplTestBase::expectClientCreate(
    size_t index, const HostWithHealthCheckMap& health_check_map) {
  TestSession& test_session = *test_sessions_[index];
  test_session.codec_ = new NiceMock<Http::MockClientConnection>();
  ON_CALL(*test_session.codec_, protocol()).WillByDefault(testing::Return(Http::Protocol::Http11));
  test_session.client_connection_ = new NiceMock<Network::MockClientConnection>();
  connection_index_.push_back(index);
  codec_index_.push_back(index);

  EXPECT_CALL(context_.dispatcher_, createClientConnection_(_, _, _, _))
      .Times(testing::AnyNumber())
      .WillRepeatedly(testing::InvokeWithoutArgs([&]() -> Network::ClientConnection* {
        uint32_t index = connection_index_.front();
        connection_index_.pop_front();
        return test_sessions_[index]->client_connection_;
      }));
  EXPECT_CALL(*health_checker_, createCodecClient_(_))
      .WillRepeatedly(
          Invoke([&](Upstream::Host::CreateConnectionData& conn_data) -> Http::CodecClient* {
            if (!health_check_map.empty()) {
              const auto& health_check_config =
                  health_check_map.at(conn_data.host_description_->address()->asString());
              // To make sure health checker checks the correct port.
              EXPECT_EQ(health_check_config.port_value(),
                        conn_data.host_description_->healthCheckAddress()->ip()->port());
            }
            uint32_t index = codec_index_.front();
            codec_index_.pop_front();
            TestSession& test_session = *test_sessions_[index];
            std::shared_ptr<Upstream::MockClusterInfo> cluster{
                new NiceMock<Upstream::MockClusterInfo>()};
            Event::MockDispatcher dispatcher_;
            test_session.codec_client_ = new CodecClientForTest(
                Http::CodecType::HTTP1, std::move(conn_data.connection_), test_session.codec_,
                nullptr,
                Upstream::makeTestHost(cluster, "tcp://127.0.0.1:9000", dispatcher_.timeSource()),
                dispatcher_);
            return test_session.codec_client_;
          }));
}

void HttpHealthCheckerImplTestBase::expectStreamCreate(size_t index) {
  test_sessions_[index]->request_encoder_.stream_.callbacks_.clear();
  EXPECT_CALL(*test_sessions_[index]->codec_, newStream(_))
      .WillOnce(DoAll(SaveArgAddress(&test_sessions_[index]->stream_response_callbacks_),
                      ReturnRef(test_sessions_[index]->request_encoder_)));
}

void HttpHealthCheckerImplTestBase::expectSessionCreate() {
  expectSessionCreate(health_checker_map_);
}
void HttpHealthCheckerImplTestBase::expectClientCreate(size_t index) {
  expectClientCreate(index, health_checker_map_);
}

// This is needed to put expectations in LIFO order. The unit tests use inSequence, which makes
// expectations FIFO.
void TcpHealthCheckerImplTestBase::expectSessionCreate() {
  timeout_timer_ = new NiceMock<Event::MockTimer>(&context_.dispatcher_);
  interval_timer_ = new NiceMock<Event::MockTimer>(&context_.dispatcher_);
}

void TcpHealthCheckerImplTestBase::expectClientCreate() {
  connection_ = new NiceMock<Network::MockClientConnection>();
  EXPECT_CALL(context_.dispatcher_, createClientConnection_(_, _, _, _))
      .WillOnce(testing::Return(connection_));
  EXPECT_CALL(*connection_, addReadFilter(_)).WillOnce(testing::SaveArg<0>(&read_filter_));
}

GrpcHealthCheckerImplTestBaseUtils::GrpcHealthCheckerImplTestBaseUtils() {
  EXPECT_CALL(*cluster_->info_, features())
      .WillRepeatedly(testing::Return(Upstream::ClusterInfo::Features::HTTP2));
}

void GrpcHealthCheckerImplTestBaseUtils::expectSessionCreate() {
  // Expectations are in LIFO order.
  TestSessionPtr new_test_session(new TestSession());
  test_sessions_.emplace_back(std::move(new_test_session));
  TestSession& test_session = *test_sessions_.back();
  test_session.timeout_timer_ = new Event::MockTimer(&context_.dispatcher_);
  test_session.interval_timer_ = new Event::MockTimer(&context_.dispatcher_);
  expectClientCreate(test_sessions_.size() - 1);
}

void GrpcHealthCheckerImplTestBaseUtils::expectClientCreate(size_t index) {
  TestSession& test_session = *test_sessions_[index];
  test_session.codec_ = new NiceMock<Http::MockClientConnection>();
  test_session.client_connection_ = new NiceMock<Network::MockClientConnection>();
  connection_index_.push_back(index);
  codec_index_.push_back(index);

  EXPECT_CALL(context_.dispatcher_, createClientConnection_(_, _, _, _))
      .Times(testing::AnyNumber())
      .WillRepeatedly(testing::InvokeWithoutArgs([&]() -> Network::ClientConnection* {
        uint32_t index = connection_index_.front();
        connection_index_.pop_front();
        return test_sessions_[index]->client_connection_;
      }));

  EXPECT_CALL(*health_checker_, createCodecClient_(_))
      .WillRepeatedly(
          Invoke([&](Upstream::Host::CreateConnectionData& conn_data) -> Http::CodecClient* {
            uint32_t index = codec_index_.front();
            codec_index_.pop_front();
            TestSession& test_session = *test_sessions_[index];
            std::shared_ptr<Upstream::MockClusterInfo> cluster{
                new NiceMock<Upstream::MockClusterInfo>()};
            Event::MockDispatcher dispatcher_;

            test_session.codec_client_ = new CodecClientForTest(
                Http::CodecType::HTTP1, std::move(conn_data.connection_), test_session.codec_,
                nullptr,
                Upstream::makeTestHost(cluster, "tcp://127.0.0.1:9000", dispatcher_.timeSource()),
                dispatcher_);
            return test_session.codec_client_;
          }));
}

void GrpcHealthCheckerImplTestBaseUtils::expectStreamCreate(size_t index) {
  test_sessions_[index]->request_encoder_.stream_.callbacks_.clear();
  EXPECT_CALL(*test_sessions_[index]->codec_, newStream(_))
      .WillOnce(DoAll(SaveArgAddress(&test_sessions_[index]->stream_response_callbacks_),
                      ReturnRef(test_sessions_[index]->request_encoder_)));
}

} // namespace Upstream
} // namespace Envoy
