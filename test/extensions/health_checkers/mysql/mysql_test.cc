#include "extensions/health_checkers/mysql/mysql.h"
#include "extensions/health_checkers/mysql/utility.h"

#include "test/common/buffer/utility.h"
#include "test/common/upstream/utility.h"
#include "test/mocks/buffer/mocks.h"
#include "test/mocks/network/mocks.h"
#include "test/mocks/runtime/mocks.h"
#include "test/mocks/upstream/mocks.h"

using testing::DoAll;
using testing::InSequence;
using testing::NiceMock;
using testing::Ref;
using testing::Return;
using testing::ReturnRef;
using testing::SaveArg;
using testing::WithArg;
using testing::_;

namespace Envoy {
namespace Extensions {
namespace HealthCheckers {
namespace MySQLHealthChecker {

using namespace Upstream;

class MySQLHealthCheckerImplTest : public testing::Test {
public:
  MySQLHealthCheckerImplTest()
      : cluster_(new NiceMock<MockCluster>()),
        event_logger_(new Upstream::MockHealthCheckEventLogger()) {}

  void setupReuseConnection() {
    const std::string yaml = R"EOF(
    timeout: 1s
    interval: 1s
    no_traffic_interval: 5s
    interval_jitter: 1s
    unhealthy_threshold: 1
    healthy_threshold: 1
    custom_health_check:
      name: envoy.health_checkers.mysql
      config:
        user: envoy-hc
    )EOF";

    const auto hc_config = parseHealthCheckFromV2Yaml(yaml);
    const auto mysql_config = getMySQLHealthCheckConfig(hc_config);

    health_checker_ = std::make_shared<MySQLHealthChecker>(
        *cluster_, hc_config, mysql_config, dispatcher_, runtime_, random_,
        Upstream::HealthCheckEventLoggerPtr(event_logger_));
  }

  void setupDontReuseConnection() {
    const std::string yaml = R"EOF(
    timeout: 1s
    interval: 1s
    no_traffic_interval: 5s
    interval_jitter: 1s
    unhealthy_threshold: 1
    healthy_threshold: 1
    reuse_connection: false
    custom_health_check:
      name: envoy.health_checkers.mysql
      config:
        user: envoy-hc
    )EOF";

    const auto hc_config = parseHealthCheckFromV2Yaml(yaml);
    const auto mysql_config = getMySQLHealthCheckConfig(hc_config);

    health_checker_ = std::make_shared<MySQLHealthChecker>(
        *cluster_, hc_config, mysql_config, dispatcher_, runtime_, random_,
        Upstream::HealthCheckEventLoggerPtr(event_logger_));
  }

  void expectSessionCreate() {
    interval_timer_ = new Event::MockTimer(&dispatcher_);
    timeout_timer_ = new Event::MockTimer(&dispatcher_);
  }

  void expectClientCreate() {
    connection_ = new NiceMock<Network::MockClientConnection>();
    EXPECT_CALL(dispatcher_, createClientConnection_(_, _, _, _)).WillOnce(Return(connection_));
    EXPECT_CALL(*connection_, addReadFilter(_)).WillOnce(SaveArg<0>(&read_filter_));
  }

  auto& build_server_greeting_packet(Buffer::Instance& buffer) {
    const char data[] = {
        '\x5b', '\x00', '\x00', '\x00', '\x0a', '\x35', '\x2e', '\x37', '\x2e', '\x32', '\x32',
        '\x2d', '\x30', '\x75', '\x62', '\x75', '\x6e', '\x74', '\x75', '\x30', '\x2e', '\x31',
        '\x36', '\x2e', '\x30', '\x34', '\x2e', '\x31', '\x00', '\x04', '\x00', '\x00', '\x00',
        '\x6d', '\x7a', '\x0c', '\x61', '\x7d', '\x44', '\x50', '\x57', '\x00', '\xff', '\xf7',
        '\x08', '\x02', '\x00', '\xff', '\x81', '\x15', '\x00', '\x00', '\x00', '\x00', '\x00',
        '\x00', '\x00', '\x00', '\x00', '\x00', '\x4f', '\x58', '\x61', '\x0c', '\x6e', '\x01',
        '\x1a', '\x66', '\x15', '\x1e', '\x17', '\x7c', '\x00', '\x6d', '\x79', '\x73', '\x71',
        '\x6c', '\x5f', '\x6e', '\x61', '\x74', '\x69', '\x76', '\x65', '\x5f', '\x70', '\x61',
        '\x73', '\x73', '\x77', '\x6f', '\x72', '\x64', '\x00'};
    buffer.add(data, sizeof(data));
    return buffer;
  }

  auto& build_login_quit_packet(Buffer::Instance& buffer) {
    const char data[] = {'\x0f', '\x00', '\x00', '\x01', '\x00', '\x80', '\x00', '\x00',
                         '\x01', '\x65', '\x6e', '\x76', '\x6f', '\x79', '\x2d', '\x68',
                         '\x63', '\x00', '\x00', '\x01', '\x00', '\x00', '\x00', '\x01'};
    buffer.add(data, sizeof(data));
    return buffer;
  }

  auto& build_ok_response_packet(Buffer::Instance& buffer) {
    const char data[] = {'\x03', '\x00', '\x00', '\x02', '\x00', '\x00', '\x00'};
    buffer.add(data, sizeof(data));
    return buffer;
  }

  std::shared_ptr<MockCluster> cluster_;
  NiceMock<Event::MockDispatcher> dispatcher_;
  std::shared_ptr<MySQLHealthChecker> health_checker_;
  Network::MockClientConnection* connection_{};
  Event::MockTimer* timeout_timer_{};
  Event::MockTimer* interval_timer_{};
  Network::ReadFilterSharedPtr read_filter_;
  NiceMock<Runtime::MockLoader> runtime_;
  NiceMock<Runtime::MockRandomGenerator> random_;
  Upstream::MockHealthCheckEventLogger* event_logger_{};
};

// Tests that a successful healthcheck will keep the client connected
TEST_F(MySQLHealthCheckerImplTest, Success) {
  InSequence s;

  setupReuseConnection();
  cluster_->prioritySet().getMockHostSet(0)->hosts_ = {
      makeTestHost(cluster_->info_, "tcp://127.0.0.1:80")};
  expectSessionCreate();
  expectClientCreate();
  EXPECT_CALL(*timeout_timer_, enableTimer(_));
  health_checker_->start();

  connection_->runHighWatermarkCallbacks();
  connection_->runLowWatermarkCallbacks();
  connection_->raiseEvent(Network::ConnectionEvent::Connected);

  Buffer::OwnedImpl login_quit_packet;
  EXPECT_CALL(*connection_, write(BufferEqual(&build_login_quit_packet(login_quit_packet)), false))
      .Times(1);
  Buffer::OwnedImpl server_greeting_packet;
  read_filter_->onData(build_server_greeting_packet(server_greeting_packet), false);
  EXPECT_CALL(*timeout_timer_, disableTimer());
  EXPECT_CALL(*interval_timer_, enableTimer(_));
  Buffer::OwnedImpl ok_response_packet;
  read_filter_->onData(build_ok_response_packet(ok_response_packet), false);

  EXPECT_FALSE(cluster_->prioritySet().getMockHostSet(0)->hosts_[0]->healthFlagGet(
      Host::HealthFlag::FAILED_ACTIVE_HC));
  EXPECT_TRUE(cluster_->prioritySet().getMockHostSet(0)->hosts_[0]->healthy());
  EXPECT_EQ(Network::Connection::State::Open, connection_->state());
}

// Tests that a successful healthcheck will disconnect the client when reuse_connection is false.
TEST_F(MySQLHealthCheckerImplTest, SuccessWithoutReusingConnection) {
  InSequence s;

  setupDontReuseConnection();
  cluster_->prioritySet().getMockHostSet(0)->hosts_ = {
      makeTestHost(cluster_->info_, "tcp://127.0.0.1:80")};
  expectSessionCreate();
  expectClientCreate();
  EXPECT_CALL(*timeout_timer_, enableTimer(_));
  health_checker_->start();

  connection_->runHighWatermarkCallbacks();
  connection_->runLowWatermarkCallbacks();
  connection_->raiseEvent(Network::ConnectionEvent::Connected);

  Buffer::OwnedImpl login_quit_packet;
  EXPECT_CALL(*connection_, write(BufferEqual(&build_login_quit_packet(login_quit_packet)), false))
      .Times(1);
  Buffer::OwnedImpl server_greeting_packet;
  read_filter_->onData(build_server_greeting_packet(server_greeting_packet), false);
  EXPECT_CALL(*timeout_timer_, disableTimer());
  EXPECT_CALL(*interval_timer_, enableTimer(_));
  EXPECT_CALL(*connection_, close(Network::ConnectionCloseType::NoFlush)).Times(1);
  Buffer::OwnedImpl ok_response_packet;
  read_filter_->onData(build_ok_response_packet(ok_response_packet), false);

  EXPECT_FALSE(cluster_->prioritySet().getMockHostSet(0)->hosts_[0]->healthFlagGet(
      Host::HealthFlag::FAILED_ACTIVE_HC));
  EXPECT_TRUE(cluster_->prioritySet().getMockHostSet(0)->hosts_[0]->healthy());
  EXPECT_EQ(Network::Connection::State::Closed, connection_->state());
}

// Tests that a corrupt server greeting packet fails the health check immediately
TEST_F(MySQLHealthCheckerImplTest, BadServerGreeting_EssentialDataTruncated) {
  InSequence s;

  setupReuseConnection();
  cluster_->prioritySet().getMockHostSet(0)->hosts_ = {
      makeTestHost(cluster_->info_, "tcp://127.0.0.1:80")};
  expectSessionCreate();
  expectClientCreate();
  EXPECT_CALL(*timeout_timer_, enableTimer(_));
  health_checker_->start();

  connection_->runHighWatermarkCallbacks();
  connection_->runLowWatermarkCallbacks();
  connection_->raiseEvent(Network::ConnectionEvent::Connected);

  Buffer::OwnedImpl server_greeting_packet;
  server_greeting_packet.add("\xff\xff", 2);
  EXPECT_CALL(*connection_, write(_, _)).Times(0);
  EXPECT_CALL(*event_logger_, logEjectUnhealthy(_, _, _));
  EXPECT_CALL(*timeout_timer_, disableTimer());
  EXPECT_CALL(*interval_timer_, enableTimer(_));
  EXPECT_CALL(*connection_, close(Network::ConnectionCloseType::NoFlush)).Times(1);
  read_filter_->onData(server_greeting_packet, false);

  EXPECT_TRUE(cluster_->prioritySet().getMockHostSet(0)->hosts_[0]->healthFlagGet(
      Host::HealthFlag::FAILED_ACTIVE_HC));
  EXPECT_FALSE(cluster_->prioritySet().getMockHostSet(0)->hosts_[0]->healthy());
  EXPECT_EQ(Network::Connection::State::Closed, connection_->state());
}

// Tests that a corrupt server greeting packet fails the health check immediately
TEST_F(MySQLHealthCheckerImplTest, BadServerGreeting_PayloadLengthMismatch) {
  InSequence s;

  setupReuseConnection();
  cluster_->prioritySet().getMockHostSet(0)->hosts_ = {
      makeTestHost(cluster_->info_, "tcp://127.0.0.1:80")};
  expectSessionCreate();
  expectClientCreate();
  EXPECT_CALL(*timeout_timer_, enableTimer(_));
  health_checker_->start();

  connection_->runHighWatermarkCallbacks();
  connection_->runLowWatermarkCallbacks();
  connection_->raiseEvent(Network::ConnectionEvent::Connected);

  Buffer::OwnedImpl server_greeting_packet;
  server_greeting_packet.add("\xff\xff\xff\xff\xff", 5);
  EXPECT_CALL(*connection_, write(_, _)).Times(0);
  EXPECT_CALL(*event_logger_, logEjectUnhealthy(_, _, _));
  EXPECT_CALL(*timeout_timer_, disableTimer());
  EXPECT_CALL(*interval_timer_, enableTimer(_));
  EXPECT_CALL(*connection_, close(Network::ConnectionCloseType::NoFlush)).Times(1);
  read_filter_->onData(server_greeting_packet, false);

  EXPECT_TRUE(cluster_->prioritySet().getMockHostSet(0)->hosts_[0]->healthFlagGet(
      Host::HealthFlag::FAILED_ACTIVE_HC));
  EXPECT_FALSE(cluster_->prioritySet().getMockHostSet(0)->hosts_[0]->healthy());
  EXPECT_EQ(Network::Connection::State::Closed, connection_->state());
}

// Tests that a corrupt server greeting packet fails the health check immediately
TEST_F(MySQLHealthCheckerImplTest, BadServerGreeting_InvalidSequenceId) {
  InSequence s;

  setupReuseConnection();
  cluster_->prioritySet().getMockHostSet(0)->hosts_ = {
      makeTestHost(cluster_->info_, "tcp://127.0.0.1:80")};
  expectSessionCreate();
  expectClientCreate();
  EXPECT_CALL(*timeout_timer_, enableTimer(_));
  health_checker_->start();

  connection_->runHighWatermarkCallbacks();
  connection_->runLowWatermarkCallbacks();
  connection_->raiseEvent(Network::ConnectionEvent::Connected);

  Buffer::OwnedImpl server_greeting_packet;
  server_greeting_packet.add("\x05\x00\x00\x00\x0a", 5);
  EXPECT_CALL(*connection_, write(_, _)).Times(0);
  EXPECT_CALL(*event_logger_, logEjectUnhealthy(_, _, _));
  EXPECT_CALL(*timeout_timer_, disableTimer());
  EXPECT_CALL(*interval_timer_, enableTimer(_));
  EXPECT_CALL(*connection_, close(Network::ConnectionCloseType::NoFlush)).Times(1);
  read_filter_->onData(server_greeting_packet, false);

  EXPECT_TRUE(cluster_->prioritySet().getMockHostSet(0)->hosts_[0]->healthFlagGet(
      Host::HealthFlag::FAILED_ACTIVE_HC));
  EXPECT_FALSE(cluster_->prioritySet().getMockHostSet(0)->hosts_[0]->healthy());
  EXPECT_EQ(Network::Connection::State::Closed, connection_->state());
}

// Tests that a corrupt server greeting packet fails the health check immediately
TEST_F(MySQLHealthCheckerImplTest, BadServerGreeting_UnsupportedProtocolVersion) {
  InSequence s;

  setupReuseConnection();
  cluster_->prioritySet().getMockHostSet(0)->hosts_ = {
      makeTestHost(cluster_->info_, "tcp://127.0.0.1:80")};
  expectSessionCreate();
  expectClientCreate();
  EXPECT_CALL(*timeout_timer_, enableTimer(_));
  health_checker_->start();

  connection_->runHighWatermarkCallbacks();
  connection_->runLowWatermarkCallbacks();
  connection_->raiseEvent(Network::ConnectionEvent::Connected);

  Buffer::OwnedImpl server_greeting_packet;
  server_greeting_packet.add("\x05\x00\x00\x00\xaa", 5);
  EXPECT_CALL(*connection_, write(_, _)).Times(0);
  EXPECT_CALL(*event_logger_, logEjectUnhealthy(_, _, _));
  EXPECT_CALL(*timeout_timer_, disableTimer());
  EXPECT_CALL(*interval_timer_, enableTimer(_));
  EXPECT_CALL(*connection_, close(Network::ConnectionCloseType::NoFlush)).Times(1);
  read_filter_->onData(server_greeting_packet, false);

  EXPECT_TRUE(cluster_->prioritySet().getMockHostSet(0)->hosts_[0]->healthFlagGet(
      Host::HealthFlag::FAILED_ACTIVE_HC));
  EXPECT_FALSE(cluster_->prioritySet().getMockHostSet(0)->hosts_[0]->healthy());
  EXPECT_EQ(Network::Connection::State::Closed, connection_->state());
}

// Tests that a corrupt server greeting packet fails the health check immediately
TEST_F(MySQLHealthCheckerImplTest, BadServerGreeting_MalformedServerVersion) {
  InSequence s;

  setupReuseConnection();
  cluster_->prioritySet().getMockHostSet(0)->hosts_ = {
      makeTestHost(cluster_->info_, "tcp://127.0.0.1:80")};
  expectSessionCreate();
  expectClientCreate();
  EXPECT_CALL(*timeout_timer_, enableTimer(_));
  health_checker_->start();

  connection_->runHighWatermarkCallbacks();
  connection_->runLowWatermarkCallbacks();
  connection_->raiseEvent(Network::ConnectionEvent::Connected);

  Buffer::OwnedImpl server_greeting_packet;
  server_greeting_packet.add("\x13\x00\x00\x00\x0aserver_version", 19);
  EXPECT_CALL(*connection_, write(_, _)).Times(0);
  EXPECT_CALL(*event_logger_, logEjectUnhealthy(_, _, _));
  EXPECT_CALL(*timeout_timer_, disableTimer());
  EXPECT_CALL(*interval_timer_, enableTimer(_));
  EXPECT_CALL(*connection_, close(Network::ConnectionCloseType::NoFlush)).Times(1);
  read_filter_->onData(server_greeting_packet, false);

  EXPECT_TRUE(cluster_->prioritySet().getMockHostSet(0)->hosts_[0]->healthFlagGet(
      Host::HealthFlag::FAILED_ACTIVE_HC));
  EXPECT_FALSE(cluster_->prioritySet().getMockHostSet(0)->hosts_[0]->healthy());
  EXPECT_EQ(Network::Connection::State::Closed, connection_->state());
}

// Tests that a corrupt server greeting packet fails the health check immediately
TEST_F(MySQLHealthCheckerImplTest, BadServerGreeting_PacketTruncated) {
  InSequence s;

  setupReuseConnection();
  cluster_->prioritySet().getMockHostSet(0)->hosts_ = {
      makeTestHost(cluster_->info_, "tcp://127.0.0.1:80")};
  expectSessionCreate();
  expectClientCreate();
  EXPECT_CALL(*timeout_timer_, enableTimer(_));
  health_checker_->start();

  connection_->runHighWatermarkCallbacks();
  connection_->runLowWatermarkCallbacks();
  connection_->raiseEvent(Network::ConnectionEvent::Connected);

  Buffer::OwnedImpl server_greeting_packet;
  server_greeting_packet.add("\x14\x00\x00\x00\x0aserver_version\x00", 20);
  EXPECT_CALL(*connection_, write(_, _)).Times(0);
  EXPECT_CALL(*event_logger_, logEjectUnhealthy(_, _, _));
  EXPECT_CALL(*timeout_timer_, disableTimer());
  EXPECT_CALL(*interval_timer_, enableTimer(_));
  EXPECT_CALL(*connection_, close(Network::ConnectionCloseType::NoFlush)).Times(1);
  read_filter_->onData(server_greeting_packet, false);

  EXPECT_TRUE(cluster_->prioritySet().getMockHostSet(0)->hosts_[0]->healthFlagGet(
      Host::HealthFlag::FAILED_ACTIVE_HC));
  EXPECT_FALSE(cluster_->prioritySet().getMockHostSet(0)->hosts_[0]->healthy());
  EXPECT_EQ(Network::Connection::State::Closed, connection_->state());
}

// Tests that a corrupt server greeting packet fails the health check immediately
TEST_F(MySQLHealthCheckerImplTest, BadServerGreeting_Client41Unsupported) {
  InSequence s;

  setupReuseConnection();
  cluster_->prioritySet().getMockHostSet(0)->hosts_ = {
      makeTestHost(cluster_->info_, "tcp://127.0.0.1:80")};
  expectSessionCreate();
  expectClientCreate();
  EXPECT_CALL(*timeout_timer_, enableTimer(_));
  health_checker_->start();

  connection_->runHighWatermarkCallbacks();
  connection_->runLowWatermarkCallbacks();
  connection_->raiseEvent(Network::ConnectionEvent::Connected);

  Buffer::OwnedImpl server_greeting_packet;
  server_greeting_packet.add("\x24\x00\x00\x00\x0aserver_"
                             "version\x00\x04\x00\x00\x00\x6d\x7a\x0c\x61\x7d\x44\x50\x57\x00\x00"
                             "\x00",
                             36);
  EXPECT_CALL(*connection_, write(_, _)).Times(0);
  EXPECT_CALL(*event_logger_, logEjectUnhealthy(_, _, _));
  EXPECT_CALL(*timeout_timer_, disableTimer());
  EXPECT_CALL(*interval_timer_, enableTimer(_));
  EXPECT_CALL(*connection_, close(Network::ConnectionCloseType::NoFlush)).Times(1);
  read_filter_->onData(server_greeting_packet, false);

  EXPECT_TRUE(cluster_->prioritySet().getMockHostSet(0)->hosts_[0]->healthFlagGet(
      Host::HealthFlag::FAILED_ACTIVE_HC));
  EXPECT_FALSE(cluster_->prioritySet().getMockHostSet(0)->hosts_[0]->healthy());
  EXPECT_EQ(Network::Connection::State::Closed, connection_->state());
}

// Tests that a corrupt server greeting packet fails the health check immediately
TEST_F(MySQLHealthCheckerImplTest,
       BadServerGreetingWithoutReusingConnection_EssentialDataTruncated) {
  InSequence s;

  setupDontReuseConnection();
  cluster_->prioritySet().getMockHostSet(0)->hosts_ = {
      makeTestHost(cluster_->info_, "tcp://127.0.0.1:80")};
  expectSessionCreate();
  expectClientCreate();
  EXPECT_CALL(*timeout_timer_, enableTimer(_));
  health_checker_->start();

  connection_->runHighWatermarkCallbacks();
  connection_->runLowWatermarkCallbacks();
  connection_->raiseEvent(Network::ConnectionEvent::Connected);

  Buffer::OwnedImpl server_greeting_packet;
  server_greeting_packet.add("\xff\xff", 2);
  EXPECT_CALL(*connection_, write(_, _)).Times(0);
  EXPECT_CALL(*event_logger_, logEjectUnhealthy(_, _, _));
  EXPECT_CALL(*timeout_timer_, disableTimer());
  EXPECT_CALL(*interval_timer_, enableTimer(_));
  EXPECT_CALL(*connection_, close(Network::ConnectionCloseType::NoFlush)).Times(1);
  read_filter_->onData(server_greeting_packet, false);

  EXPECT_TRUE(cluster_->prioritySet().getMockHostSet(0)->hosts_[0]->healthFlagGet(
      Host::HealthFlag::FAILED_ACTIVE_HC));
  EXPECT_FALSE(cluster_->prioritySet().getMockHostSet(0)->hosts_[0]->healthy());
  EXPECT_EQ(Network::Connection::State::Closed, connection_->state());
}

// Tests that a corrupt server greeting packet fails the health check immediately
TEST_F(MySQLHealthCheckerImplTest,
       BadServerGreetingWithoutReusingConnection_PayloadLengthMismatch) {
  InSequence s;

  setupDontReuseConnection();
  cluster_->prioritySet().getMockHostSet(0)->hosts_ = {
      makeTestHost(cluster_->info_, "tcp://127.0.0.1:80")};
  expectSessionCreate();
  expectClientCreate();
  EXPECT_CALL(*timeout_timer_, enableTimer(_));
  health_checker_->start();

  connection_->runHighWatermarkCallbacks();
  connection_->runLowWatermarkCallbacks();
  connection_->raiseEvent(Network::ConnectionEvent::Connected);

  Buffer::OwnedImpl server_greeting_packet;
  server_greeting_packet.add("\xff\xff\xff\xff\xff", 5);
  EXPECT_CALL(*connection_, write(_, _)).Times(0);
  EXPECT_CALL(*event_logger_, logEjectUnhealthy(_, _, _));
  EXPECT_CALL(*timeout_timer_, disableTimer());
  EXPECT_CALL(*interval_timer_, enableTimer(_));
  EXPECT_CALL(*connection_, close(Network::ConnectionCloseType::NoFlush)).Times(1);
  read_filter_->onData(server_greeting_packet, false);

  EXPECT_TRUE(cluster_->prioritySet().getMockHostSet(0)->hosts_[0]->healthFlagGet(
      Host::HealthFlag::FAILED_ACTIVE_HC));
  EXPECT_FALSE(cluster_->prioritySet().getMockHostSet(0)->hosts_[0]->healthy());
  EXPECT_EQ(Network::Connection::State::Closed, connection_->state());
}

// Tests that a corrupt server greeting packet fails the health check immediately
TEST_F(MySQLHealthCheckerImplTest, BadServerGreetingWithoutReusingConnection_InvalidSequenceId) {
  InSequence s;

  setupDontReuseConnection();
  cluster_->prioritySet().getMockHostSet(0)->hosts_ = {
      makeTestHost(cluster_->info_, "tcp://127.0.0.1:80")};
  expectSessionCreate();
  expectClientCreate();
  EXPECT_CALL(*timeout_timer_, enableTimer(_));
  health_checker_->start();

  connection_->runHighWatermarkCallbacks();
  connection_->runLowWatermarkCallbacks();
  connection_->raiseEvent(Network::ConnectionEvent::Connected);

  Buffer::OwnedImpl server_greeting_packet;
  server_greeting_packet.add("\x05\x00\x00\x00\x0a", 5);
  EXPECT_CALL(*connection_, write(_, _)).Times(0);
  EXPECT_CALL(*event_logger_, logEjectUnhealthy(_, _, _));
  EXPECT_CALL(*timeout_timer_, disableTimer());
  EXPECT_CALL(*interval_timer_, enableTimer(_));
  EXPECT_CALL(*connection_, close(Network::ConnectionCloseType::NoFlush)).Times(1);
  read_filter_->onData(server_greeting_packet, false);

  EXPECT_TRUE(cluster_->prioritySet().getMockHostSet(0)->hosts_[0]->healthFlagGet(
      Host::HealthFlag::FAILED_ACTIVE_HC));
  EXPECT_FALSE(cluster_->prioritySet().getMockHostSet(0)->hosts_[0]->healthy());
  EXPECT_EQ(Network::Connection::State::Closed, connection_->state());
}

// Tests that a corrupt server greeting packet fails the health check immediately
TEST_F(MySQLHealthCheckerImplTest,
       BadServerGreetingWithoutReusingConnection_UnsupportedProtocolVersion) {
  InSequence s;

  setupDontReuseConnection();
  cluster_->prioritySet().getMockHostSet(0)->hosts_ = {
      makeTestHost(cluster_->info_, "tcp://127.0.0.1:80")};
  expectSessionCreate();
  expectClientCreate();
  EXPECT_CALL(*timeout_timer_, enableTimer(_));
  health_checker_->start();

  connection_->runHighWatermarkCallbacks();
  connection_->runLowWatermarkCallbacks();
  connection_->raiseEvent(Network::ConnectionEvent::Connected);

  Buffer::OwnedImpl server_greeting_packet;
  server_greeting_packet.add("\x05\x00\x00\x00\xaa", 5);
  EXPECT_CALL(*connection_, write(_, _)).Times(0);
  EXPECT_CALL(*event_logger_, logEjectUnhealthy(_, _, _));
  EXPECT_CALL(*timeout_timer_, disableTimer());
  EXPECT_CALL(*interval_timer_, enableTimer(_));
  EXPECT_CALL(*connection_, close(Network::ConnectionCloseType::NoFlush)).Times(1);
  read_filter_->onData(server_greeting_packet, false);

  EXPECT_TRUE(cluster_->prioritySet().getMockHostSet(0)->hosts_[0]->healthFlagGet(
      Host::HealthFlag::FAILED_ACTIVE_HC));
  EXPECT_FALSE(cluster_->prioritySet().getMockHostSet(0)->hosts_[0]->healthy());
  EXPECT_EQ(Network::Connection::State::Closed, connection_->state());
}

// Tests that a corrupt server greeting packet fails the health check immediately
TEST_F(MySQLHealthCheckerImplTest,
       BadServerGreetingWithoutReusingConnection_MalformedServerVersion) {
  InSequence s;

  setupDontReuseConnection();
  cluster_->prioritySet().getMockHostSet(0)->hosts_ = {
      makeTestHost(cluster_->info_, "tcp://127.0.0.1:80")};
  expectSessionCreate();
  expectClientCreate();
  EXPECT_CALL(*timeout_timer_, enableTimer(_));
  health_checker_->start();

  connection_->runHighWatermarkCallbacks();
  connection_->runLowWatermarkCallbacks();
  connection_->raiseEvent(Network::ConnectionEvent::Connected);

  Buffer::OwnedImpl server_greeting_packet;
  server_greeting_packet.add("\x13\x00\x00\x00\x0aserver_version", 19);
  EXPECT_CALL(*connection_, write(_, _)).Times(0);
  EXPECT_CALL(*event_logger_, logEjectUnhealthy(_, _, _));
  EXPECT_CALL(*timeout_timer_, disableTimer());
  EXPECT_CALL(*interval_timer_, enableTimer(_));
  EXPECT_CALL(*connection_, close(Network::ConnectionCloseType::NoFlush)).Times(1);
  read_filter_->onData(server_greeting_packet, false);

  EXPECT_TRUE(cluster_->prioritySet().getMockHostSet(0)->hosts_[0]->healthFlagGet(
      Host::HealthFlag::FAILED_ACTIVE_HC));
  EXPECT_FALSE(cluster_->prioritySet().getMockHostSet(0)->hosts_[0]->healthy());
  EXPECT_EQ(Network::Connection::State::Closed, connection_->state());
}

// Tests that a corrupt server greeting packet fails the health check immediately
TEST_F(MySQLHealthCheckerImplTest, BadServerGreetingWithoutReusingConnection_PacketTruncated) {
  InSequence s;

  setupDontReuseConnection();
  cluster_->prioritySet().getMockHostSet(0)->hosts_ = {
      makeTestHost(cluster_->info_, "tcp://127.0.0.1:80")};
  expectSessionCreate();
  expectClientCreate();
  EXPECT_CALL(*timeout_timer_, enableTimer(_));
  health_checker_->start();

  connection_->runHighWatermarkCallbacks();
  connection_->runLowWatermarkCallbacks();
  connection_->raiseEvent(Network::ConnectionEvent::Connected);

  Buffer::OwnedImpl server_greeting_packet;
  server_greeting_packet.add("\x14\x00\x00\x00\x0aserver_version\x00", 20);
  EXPECT_CALL(*connection_, write(_, _)).Times(0);
  EXPECT_CALL(*event_logger_, logEjectUnhealthy(_, _, _));
  EXPECT_CALL(*timeout_timer_, disableTimer());
  EXPECT_CALL(*interval_timer_, enableTimer(_));
  EXPECT_CALL(*connection_, close(Network::ConnectionCloseType::NoFlush)).Times(1);
  read_filter_->onData(server_greeting_packet, false);

  EXPECT_TRUE(cluster_->prioritySet().getMockHostSet(0)->hosts_[0]->healthFlagGet(
      Host::HealthFlag::FAILED_ACTIVE_HC));
  EXPECT_FALSE(cluster_->prioritySet().getMockHostSet(0)->hosts_[0]->healthy());
  EXPECT_EQ(Network::Connection::State::Closed, connection_->state());
}

// Tests that a corrupt server greeting packet fails the health check immediately
TEST_F(MySQLHealthCheckerImplTest, BadServerGreetingWithoutReusingConnection_Client41Unsupported) {
  InSequence s;

  setupDontReuseConnection();
  cluster_->prioritySet().getMockHostSet(0)->hosts_ = {
      makeTestHost(cluster_->info_, "tcp://127.0.0.1:80")};
  expectSessionCreate();
  expectClientCreate();
  EXPECT_CALL(*timeout_timer_, enableTimer(_));
  health_checker_->start();

  connection_->runHighWatermarkCallbacks();
  connection_->runLowWatermarkCallbacks();
  connection_->raiseEvent(Network::ConnectionEvent::Connected);

  Buffer::OwnedImpl server_greeting_packet;
  server_greeting_packet.add("\x24\x00\x00\x00\x0aserver_"
                             "version\x00\x04\x00\x00\x00\x6d\x7a\x0c\x61\x7d\x44\x50\x57\x00\x00"
                             "\x00",
                             36);
  EXPECT_CALL(*connection_, write(_, _)).Times(0);
  EXPECT_CALL(*event_logger_, logEjectUnhealthy(_, _, _));
  EXPECT_CALL(*timeout_timer_, disableTimer());
  EXPECT_CALL(*interval_timer_, enableTimer(_));
  EXPECT_CALL(*connection_, close(Network::ConnectionCloseType::NoFlush)).Times(1);
  read_filter_->onData(server_greeting_packet, false);

  EXPECT_TRUE(cluster_->prioritySet().getMockHostSet(0)->hosts_[0]->healthFlagGet(
      Host::HealthFlag::FAILED_ACTIVE_HC));
  EXPECT_FALSE(cluster_->prioritySet().getMockHostSet(0)->hosts_[0]->healthy());
  EXPECT_EQ(Network::Connection::State::Closed, connection_->state());
}

// Tests that a corrupt server reply packet fails the health check
TEST_F(MySQLHealthCheckerImplTest, BadServerReply_PacketTruncated) {
  InSequence s;

  setupReuseConnection();
  cluster_->prioritySet().getMockHostSet(0)->hosts_ = {
      makeTestHost(cluster_->info_, "tcp://127.0.0.1:80")};
  expectSessionCreate();
  expectClientCreate();
  EXPECT_CALL(*timeout_timer_, enableTimer(_));
  health_checker_->start();

  connection_->runHighWatermarkCallbacks();
  connection_->runLowWatermarkCallbacks();
  connection_->raiseEvent(Network::ConnectionEvent::Connected);

  Buffer::OwnedImpl login_quit_packet;
  EXPECT_CALL(*connection_, write(BufferEqual(&build_login_quit_packet(login_quit_packet)), false))
      .Times(1);
  Buffer::OwnedImpl server_greeting_packet;
  read_filter_->onData(build_server_greeting_packet(server_greeting_packet), false);
  EXPECT_CALL(*event_logger_, logEjectUnhealthy(_, _, _));
  EXPECT_CALL(*timeout_timer_, disableTimer());
  EXPECT_CALL(*interval_timer_, enableTimer(_));

  Buffer::OwnedImpl ok_response_packet;
  ok_response_packet.add("\x04\x00\x00\x02", 4);
  read_filter_->onData(ok_response_packet, false);

  EXPECT_TRUE(cluster_->prioritySet().getMockHostSet(0)->hosts_[0]->healthFlagGet(
      Host::HealthFlag::FAILED_ACTIVE_HC));
  EXPECT_FALSE(cluster_->prioritySet().getMockHostSet(0)->hosts_[0]->healthy());
  EXPECT_EQ(Network::Connection::State::Closed, connection_->state());
}

// Tests that a corrupt server reply packet fails the health check
TEST_F(MySQLHealthCheckerImplTest, BadServerReply_PayloadLengthMismatch) {
  InSequence s;

  setupReuseConnection();
  cluster_->prioritySet().getMockHostSet(0)->hosts_ = {
      makeTestHost(cluster_->info_, "tcp://127.0.0.1:80")};
  expectSessionCreate();
  expectClientCreate();
  EXPECT_CALL(*timeout_timer_, enableTimer(_));
  health_checker_->start();

  connection_->runHighWatermarkCallbacks();
  connection_->runLowWatermarkCallbacks();
  connection_->raiseEvent(Network::ConnectionEvent::Connected);

  Buffer::OwnedImpl login_quit_packet;
  EXPECT_CALL(*connection_, write(BufferEqual(&build_login_quit_packet(login_quit_packet)), false))
      .Times(1);
  Buffer::OwnedImpl server_greeting_packet;
  read_filter_->onData(build_server_greeting_packet(server_greeting_packet), false);
  EXPECT_CALL(*event_logger_, logEjectUnhealthy(_, _, _));
  EXPECT_CALL(*timeout_timer_, disableTimer());
  EXPECT_CALL(*interval_timer_, enableTimer(_));

  Buffer::OwnedImpl ok_response_packet;
  ok_response_packet.add("\x05\x00\x00\x02\x00\x00\x00", 7);
  read_filter_->onData(ok_response_packet, false);

  EXPECT_TRUE(cluster_->prioritySet().getMockHostSet(0)->hosts_[0]->healthFlagGet(
      Host::HealthFlag::FAILED_ACTIVE_HC));
  EXPECT_FALSE(cluster_->prioritySet().getMockHostSet(0)->hosts_[0]->healthy());
  EXPECT_EQ(Network::Connection::State::Closed, connection_->state());
}

// Tests that a corrupt server reply packet fails the health check
TEST_F(MySQLHealthCheckerImplTest, BadServerReply_InvalidSequenceId) {
  InSequence s;

  setupReuseConnection();
  cluster_->prioritySet().getMockHostSet(0)->hosts_ = {
      makeTestHost(cluster_->info_, "tcp://127.0.0.1:80")};
  expectSessionCreate();
  expectClientCreate();
  EXPECT_CALL(*timeout_timer_, enableTimer(_));
  health_checker_->start();

  connection_->runHighWatermarkCallbacks();
  connection_->runLowWatermarkCallbacks();
  connection_->raiseEvent(Network::ConnectionEvent::Connected);

  Buffer::OwnedImpl login_quit_packet;
  EXPECT_CALL(*connection_, write(BufferEqual(&build_login_quit_packet(login_quit_packet)), false))
      .Times(1);
  Buffer::OwnedImpl server_greeting_packet;
  read_filter_->onData(build_server_greeting_packet(server_greeting_packet), false);
  EXPECT_CALL(*event_logger_, logEjectUnhealthy(_, _, _));
  EXPECT_CALL(*timeout_timer_, disableTimer());
  EXPECT_CALL(*interval_timer_, enableTimer(_));

  Buffer::OwnedImpl ok_response_packet;
  ok_response_packet.add("\x07\x00\x00\x01\x00\x00\x00", 7);
  read_filter_->onData(ok_response_packet, false);

  EXPECT_TRUE(cluster_->prioritySet().getMockHostSet(0)->hosts_[0]->healthFlagGet(
      Host::HealthFlag::FAILED_ACTIVE_HC));
  EXPECT_FALSE(cluster_->prioritySet().getMockHostSet(0)->hosts_[0]->healthy());
  EXPECT_EQ(Network::Connection::State::Closed, connection_->state());
}

// Tests that a corrupt server reply packet fails the health check
TEST_F(MySQLHealthCheckerImplTest, BadServerReply_UnexpectedResponse) {
  InSequence s;

  setupReuseConnection();
  cluster_->prioritySet().getMockHostSet(0)->hosts_ = {
      makeTestHost(cluster_->info_, "tcp://127.0.0.1:80")};
  expectSessionCreate();
  expectClientCreate();
  EXPECT_CALL(*timeout_timer_, enableTimer(_));
  health_checker_->start();

  connection_->runHighWatermarkCallbacks();
  connection_->runLowWatermarkCallbacks();
  connection_->raiseEvent(Network::ConnectionEvent::Connected);

  Buffer::OwnedImpl login_quit_packet;
  EXPECT_CALL(*connection_, write(BufferEqual(&build_login_quit_packet(login_quit_packet)), false))
      .Times(1);
  Buffer::OwnedImpl server_greeting_packet;
  read_filter_->onData(build_server_greeting_packet(server_greeting_packet), false);
  EXPECT_CALL(*event_logger_, logEjectUnhealthy(_, _, _));
  EXPECT_CALL(*timeout_timer_, disableTimer());
  EXPECT_CALL(*interval_timer_, enableTimer(_));

  Buffer::OwnedImpl ok_response_packet;
  ok_response_packet.add("\x07\x00\x00\x02\x07\x00\x00", 7);
  read_filter_->onData(ok_response_packet, false);

  EXPECT_TRUE(cluster_->prioritySet().getMockHostSet(0)->hosts_[0]->healthFlagGet(
      Host::HealthFlag::FAILED_ACTIVE_HC));
  EXPECT_FALSE(cluster_->prioritySet().getMockHostSet(0)->hosts_[0]->healthy());
  EXPECT_EQ(Network::Connection::State::Closed, connection_->state());
}

// Tests that a corrupt server reply packet fails the health check
TEST_F(MySQLHealthCheckerImplTest, BadServerReply_WrongPacketThreshold) {
  InSequence s;

  setupReuseConnection();
  cluster_->prioritySet().getMockHostSet(0)->hosts_ = {
      makeTestHost(cluster_->info_, "tcp://127.0.0.1:80")};
  expectSessionCreate();
  expectClientCreate();
  EXPECT_CALL(*timeout_timer_, enableTimer(_));
  health_checker_->start();

  connection_->runHighWatermarkCallbacks();
  connection_->runLowWatermarkCallbacks();
  connection_->raiseEvent(Network::ConnectionEvent::Connected);

  Buffer::OwnedImpl login_quit_packet;
  EXPECT_CALL(*connection_, write(BufferEqual(&build_login_quit_packet(login_quit_packet)), false))
      .Times(1);
  Buffer::OwnedImpl server_greeting_packet;
  read_filter_->onData(build_server_greeting_packet(server_greeting_packet), false);
  EXPECT_CALL(*event_logger_, logEjectUnhealthy(_, _, _));
  EXPECT_CALL(*timeout_timer_, disableTimer());
  EXPECT_CALL(*interval_timer_, enableTimer(_));

  Buffer::OwnedImpl ok_response_packet;
  ok_response_packet.add("\x05\x00\x00\x02\x00", 5);
  read_filter_->onData(ok_response_packet, false);

  EXPECT_TRUE(cluster_->prioritySet().getMockHostSet(0)->hosts_[0]->healthFlagGet(
      Host::HealthFlag::FAILED_ACTIVE_HC));
  EXPECT_FALSE(cluster_->prioritySet().getMockHostSet(0)->hosts_[0]->healthy());
  EXPECT_EQ(Network::Connection::State::Closed, connection_->state());
}

// Tests that a corrupt server reply packet fails the health check
TEST_F(MySQLHealthCheckerImplTest, BadServerReplyWithoutReusingConnection_PacketTruncated) {
  InSequence s;

  setupDontReuseConnection();
  cluster_->prioritySet().getMockHostSet(0)->hosts_ = {
      makeTestHost(cluster_->info_, "tcp://127.0.0.1:80")};
  expectSessionCreate();
  expectClientCreate();
  EXPECT_CALL(*timeout_timer_, enableTimer(_));
  health_checker_->start();

  connection_->runHighWatermarkCallbacks();
  connection_->runLowWatermarkCallbacks();
  connection_->raiseEvent(Network::ConnectionEvent::Connected);

  Buffer::OwnedImpl login_quit_packet;
  EXPECT_CALL(*connection_, write(BufferEqual(&build_login_quit_packet(login_quit_packet)), false))
      .Times(1);
  Buffer::OwnedImpl server_greeting_packet;
  read_filter_->onData(build_server_greeting_packet(server_greeting_packet), false);
  EXPECT_CALL(*event_logger_, logEjectUnhealthy(_, _, _));
  EXPECT_CALL(*timeout_timer_, disableTimer());
  EXPECT_CALL(*interval_timer_, enableTimer(_));
  EXPECT_CALL(*connection_, close(Network::ConnectionCloseType::NoFlush)).Times(1);

  Buffer::OwnedImpl ok_response_packet;
  ok_response_packet.add("\x04\x00\x00\x02", 4);
  read_filter_->onData(ok_response_packet, false);

  EXPECT_TRUE(cluster_->prioritySet().getMockHostSet(0)->hosts_[0]->healthFlagGet(
      Host::HealthFlag::FAILED_ACTIVE_HC));
  EXPECT_FALSE(cluster_->prioritySet().getMockHostSet(0)->hosts_[0]->healthy());
  EXPECT_EQ(Network::Connection::State::Closed, connection_->state());
}

// Tests that a corrupt server reply packet fails the health check
TEST_F(MySQLHealthCheckerImplTest, BadServerReplyWithoutReusingConnection_PayloadLengthMismatch) {
  InSequence s;

  setupDontReuseConnection();
  cluster_->prioritySet().getMockHostSet(0)->hosts_ = {
      makeTestHost(cluster_->info_, "tcp://127.0.0.1:80")};
  expectSessionCreate();
  expectClientCreate();
  EXPECT_CALL(*timeout_timer_, enableTimer(_));
  health_checker_->start();

  connection_->runHighWatermarkCallbacks();
  connection_->runLowWatermarkCallbacks();
  connection_->raiseEvent(Network::ConnectionEvent::Connected);

  Buffer::OwnedImpl login_quit_packet;
  EXPECT_CALL(*connection_, write(BufferEqual(&build_login_quit_packet(login_quit_packet)), false))
      .Times(1);
  Buffer::OwnedImpl server_greeting_packet;
  read_filter_->onData(build_server_greeting_packet(server_greeting_packet), false);
  EXPECT_CALL(*event_logger_, logEjectUnhealthy(_, _, _));
  EXPECT_CALL(*timeout_timer_, disableTimer());
  EXPECT_CALL(*interval_timer_, enableTimer(_));
  EXPECT_CALL(*connection_, close(Network::ConnectionCloseType::NoFlush)).Times(1);

  Buffer::OwnedImpl ok_response_packet;
  ok_response_packet.add("\x05\x00\x00\x02\x00\x00\x00", 7);
  read_filter_->onData(ok_response_packet, false);

  EXPECT_TRUE(cluster_->prioritySet().getMockHostSet(0)->hosts_[0]->healthFlagGet(
      Host::HealthFlag::FAILED_ACTIVE_HC));
  EXPECT_FALSE(cluster_->prioritySet().getMockHostSet(0)->hosts_[0]->healthy());
  EXPECT_EQ(Network::Connection::State::Closed, connection_->state());
}

// Tests that a corrupt server reply packet fails the health check
TEST_F(MySQLHealthCheckerImplTest, BadServerReplyWithoutReusingConnection_InvalidSequenceId) {
  InSequence s;

  setupDontReuseConnection();
  cluster_->prioritySet().getMockHostSet(0)->hosts_ = {
      makeTestHost(cluster_->info_, "tcp://127.0.0.1:80")};
  expectSessionCreate();
  expectClientCreate();
  EXPECT_CALL(*timeout_timer_, enableTimer(_));
  health_checker_->start();

  connection_->runHighWatermarkCallbacks();
  connection_->runLowWatermarkCallbacks();
  connection_->raiseEvent(Network::ConnectionEvent::Connected);

  Buffer::OwnedImpl login_quit_packet;
  EXPECT_CALL(*connection_, write(BufferEqual(&build_login_quit_packet(login_quit_packet)), false))
      .Times(1);
  Buffer::OwnedImpl server_greeting_packet;
  read_filter_->onData(build_server_greeting_packet(server_greeting_packet), false);
  EXPECT_CALL(*event_logger_, logEjectUnhealthy(_, _, _));
  EXPECT_CALL(*timeout_timer_, disableTimer());
  EXPECT_CALL(*interval_timer_, enableTimer(_));
  EXPECT_CALL(*connection_, close(Network::ConnectionCloseType::NoFlush)).Times(1);

  Buffer::OwnedImpl ok_response_packet;
  ok_response_packet.add("\x07\x00\x00\x01\x00\x00\x00", 7);
  read_filter_->onData(ok_response_packet, false);

  EXPECT_TRUE(cluster_->prioritySet().getMockHostSet(0)->hosts_[0]->healthFlagGet(
      Host::HealthFlag::FAILED_ACTIVE_HC));
  EXPECT_FALSE(cluster_->prioritySet().getMockHostSet(0)->hosts_[0]->healthy());
  EXPECT_EQ(Network::Connection::State::Closed, connection_->state());
}

// Tests that a corrupt server reply packet fails the health check
TEST_F(MySQLHealthCheckerImplTest, BadServerReplyWithoutReusingConnection_UnexpectedResponse) {
  InSequence s;

  setupDontReuseConnection();
  cluster_->prioritySet().getMockHostSet(0)->hosts_ = {
      makeTestHost(cluster_->info_, "tcp://127.0.0.1:80")};
  expectSessionCreate();
  expectClientCreate();
  EXPECT_CALL(*timeout_timer_, enableTimer(_));
  health_checker_->start();

  connection_->runHighWatermarkCallbacks();
  connection_->runLowWatermarkCallbacks();
  connection_->raiseEvent(Network::ConnectionEvent::Connected);

  Buffer::OwnedImpl login_quit_packet;
  EXPECT_CALL(*connection_, write(BufferEqual(&build_login_quit_packet(login_quit_packet)), false))
      .Times(1);
  Buffer::OwnedImpl server_greeting_packet;
  read_filter_->onData(build_server_greeting_packet(server_greeting_packet), false);
  EXPECT_CALL(*event_logger_, logEjectUnhealthy(_, _, _));
  EXPECT_CALL(*timeout_timer_, disableTimer());
  EXPECT_CALL(*interval_timer_, enableTimer(_));
  EXPECT_CALL(*connection_, close(Network::ConnectionCloseType::NoFlush)).Times(1);

  Buffer::OwnedImpl ok_response_packet;
  ok_response_packet.add("\x07\x00\x00\x02\x07\x00\x00", 7);
  read_filter_->onData(ok_response_packet, false);

  EXPECT_TRUE(cluster_->prioritySet().getMockHostSet(0)->hosts_[0]->healthFlagGet(
      Host::HealthFlag::FAILED_ACTIVE_HC));
  EXPECT_FALSE(cluster_->prioritySet().getMockHostSet(0)->hosts_[0]->healthy());
  EXPECT_EQ(Network::Connection::State::Closed, connection_->state());
}

// Tests that a corrupt server reply packet fails the health check
TEST_F(MySQLHealthCheckerImplTest, BadServerReplyWithoutReusingConnection_WrongPacketThreshold) {
  InSequence s;

  setupDontReuseConnection();
  cluster_->prioritySet().getMockHostSet(0)->hosts_ = {
      makeTestHost(cluster_->info_, "tcp://127.0.0.1:80")};
  expectSessionCreate();
  expectClientCreate();
  EXPECT_CALL(*timeout_timer_, enableTimer(_));
  health_checker_->start();

  connection_->runHighWatermarkCallbacks();
  connection_->runLowWatermarkCallbacks();
  connection_->raiseEvent(Network::ConnectionEvent::Connected);

  Buffer::OwnedImpl login_quit_packet;
  EXPECT_CALL(*connection_, write(BufferEqual(&build_login_quit_packet(login_quit_packet)), false))
      .Times(1);
  Buffer::OwnedImpl server_greeting_packet;
  read_filter_->onData(build_server_greeting_packet(server_greeting_packet), false);
  EXPECT_CALL(*event_logger_, logEjectUnhealthy(_, _, _));
  EXPECT_CALL(*timeout_timer_, disableTimer());
  EXPECT_CALL(*interval_timer_, enableTimer(_));
  EXPECT_CALL(*connection_, close(Network::ConnectionCloseType::NoFlush)).Times(1);

  Buffer::OwnedImpl ok_response_packet;
  ok_response_packet.add("\x05\x00\x00\x02\x00", 5);
  read_filter_->onData(ok_response_packet, false);

  EXPECT_TRUE(cluster_->prioritySet().getMockHostSet(0)->hosts_[0]->healthFlagGet(
      Host::HealthFlag::FAILED_ACTIVE_HC));
  EXPECT_FALSE(cluster_->prioritySet().getMockHostSet(0)->hosts_[0]->healthy());
  EXPECT_EQ(Network::Connection::State::Closed, connection_->state());
}

// Tests that a timeout while waiting for the server reply fails the health check
TEST_F(MySQLHealthCheckerImplTest, ServerGreetingTimeout) {
  InSequence s;

  setupReuseConnection();
  health_checker_->start();

  expectSessionCreate();
  expectClientCreate();
  cluster_->prioritySet().getMockHostSet(0)->hosts_ = {
      makeTestHost(cluster_->info_, "tcp://127.0.0.1:80")};
  EXPECT_CALL(*timeout_timer_, enableTimer(_));

  cluster_->prioritySet().getMockHostSet(0)->runCallbacks(
      {cluster_->prioritySet().getMockHostSet(0)->hosts_.back()}, {});

  connection_->raiseEvent(Network::ConnectionEvent::Connected);

  EXPECT_CALL(*connection_, close(_));
  EXPECT_CALL(*event_logger_, logEjectUnhealthy(_, _, _));
  EXPECT_CALL(*timeout_timer_, disableTimer());
  EXPECT_CALL(*interval_timer_, enableTimer(_));
  timeout_timer_->callback_();
  EXPECT_TRUE(cluster_->prioritySet().getMockHostSet(0)->hosts_[0]->healthFlagGet(
      Host::HealthFlag::FAILED_ACTIVE_HC));
  EXPECT_FALSE(cluster_->prioritySet().getMockHostSet(0)->hosts_[0]->healthy());

  HostVector removed{cluster_->prioritySet().getMockHostSet(0)->hosts_.back()};
  cluster_->prioritySet().getMockHostSet(0)->hosts_.clear();
  cluster_->prioritySet().getMockHostSet(0)->runCallbacks({}, removed);
}

// Tests that a timeout while waiting for the server reply fails the health check
TEST_F(MySQLHealthCheckerImplTest, ServerGreetingTimeoutWithoutReusingConnection) {
  InSequence s;

  setupDontReuseConnection();
  health_checker_->start();

  expectSessionCreate();
  expectClientCreate();
  cluster_->prioritySet().getMockHostSet(0)->hosts_ = {
      makeTestHost(cluster_->info_, "tcp://127.0.0.1:80")};
  EXPECT_CALL(*timeout_timer_, enableTimer(_));

  cluster_->prioritySet().getMockHostSet(0)->runCallbacks(
      {cluster_->prioritySet().getMockHostSet(0)->hosts_.back()}, {});

  connection_->raiseEvent(Network::ConnectionEvent::Connected);

  EXPECT_CALL(*connection_, close(_));
  EXPECT_CALL(*event_logger_, logEjectUnhealthy(_, _, _));
  EXPECT_CALL(*timeout_timer_, disableTimer());
  EXPECT_CALL(*interval_timer_, enableTimer(_));
  timeout_timer_->callback_();
  EXPECT_TRUE(cluster_->prioritySet().getMockHostSet(0)->hosts_[0]->healthFlagGet(
      Host::HealthFlag::FAILED_ACTIVE_HC));
  EXPECT_FALSE(cluster_->prioritySet().getMockHostSet(0)->hosts_[0]->healthy());

  HostVector removed{cluster_->prioritySet().getMockHostSet(0)->hosts_.back()};
  cluster_->prioritySet().getMockHostSet(0)->hosts_.clear();
  cluster_->prioritySet().getMockHostSet(0)->runCallbacks({}, removed);
}

// Tests that a remote close while waiting for the server reply fails the health check
TEST_F(MySQLHealthCheckerImplTest, ServerGreetingRemoteClose) {
  InSequence s;

  setupReuseConnection();
  health_checker_->start();

  expectSessionCreate();
  expectClientCreate();
  cluster_->prioritySet().getMockHostSet(0)->hosts_ = {
      makeTestHost(cluster_->info_, "tcp://127.0.0.1:80")};
  EXPECT_CALL(*timeout_timer_, enableTimer(_));

  cluster_->prioritySet().getMockHostSet(0)->runCallbacks(
      {cluster_->prioritySet().getMockHostSet(0)->hosts_.back()}, {});

  connection_->raiseEvent(Network::ConnectionEvent::Connected);

  EXPECT_CALL(*event_logger_, logEjectUnhealthy(_, _, _));
  EXPECT_CALL(*timeout_timer_, disableTimer());
  EXPECT_CALL(*interval_timer_, enableTimer(_));
  connection_->raiseEvent(Network::ConnectionEvent::RemoteClose);
  EXPECT_TRUE(cluster_->prioritySet().getMockHostSet(0)->hosts_[0]->healthFlagGet(
      Host::HealthFlag::FAILED_ACTIVE_HC));
  EXPECT_FALSE(cluster_->prioritySet().getMockHostSet(0)->hosts_[0]->healthy());
}

// Tests that a remote close while waiting for the server reply fails the health check
TEST_F(MySQLHealthCheckerImplTest, ServerGreetingRemoteCloseWithoutReusingConnection) {
  InSequence s;

  setupDontReuseConnection();
  health_checker_->start();

  expectSessionCreate();
  expectClientCreate();
  cluster_->prioritySet().getMockHostSet(0)->hosts_ = {
      makeTestHost(cluster_->info_, "tcp://127.0.0.1:80")};
  EXPECT_CALL(*timeout_timer_, enableTimer(_));

  cluster_->prioritySet().getMockHostSet(0)->runCallbacks(
      {cluster_->prioritySet().getMockHostSet(0)->hosts_.back()}, {});

  connection_->raiseEvent(Network::ConnectionEvent::Connected);

  EXPECT_CALL(*event_logger_, logEjectUnhealthy(_, _, _));
  EXPECT_CALL(*timeout_timer_, disableTimer());
  EXPECT_CALL(*interval_timer_, enableTimer(_));
  connection_->raiseEvent(Network::ConnectionEvent::RemoteClose);
  EXPECT_TRUE(cluster_->prioritySet().getMockHostSet(0)->hosts_[0]->healthFlagGet(
      Host::HealthFlag::FAILED_ACTIVE_HC));
  EXPECT_FALSE(cluster_->prioritySet().getMockHostSet(0)->hosts_[0]->healthy());
}

// Tests that a timeout while waiting for the server reply fails the health check
TEST_F(MySQLHealthCheckerImplTest, ServerReplyTimeout) {
  InSequence s;

  setupReuseConnection();
  health_checker_->start();

  expectSessionCreate();
  expectClientCreate();
  cluster_->prioritySet().getMockHostSet(0)->hosts_ = {
      makeTestHost(cluster_->info_, "tcp://127.0.0.1:80")};
  EXPECT_CALL(*timeout_timer_, enableTimer(_));

  cluster_->prioritySet().getMockHostSet(0)->runCallbacks(
      {cluster_->prioritySet().getMockHostSet(0)->hosts_.back()}, {});

  connection_->raiseEvent(Network::ConnectionEvent::Connected);

  Buffer::OwnedImpl login_quit_packet;
  EXPECT_CALL(*connection_, write(BufferEqual(&build_login_quit_packet(login_quit_packet)), false))
      .Times(1);
  Buffer::OwnedImpl server_greeting_packet;
  read_filter_->onData(build_server_greeting_packet(server_greeting_packet), false);

  EXPECT_CALL(*connection_, close(_));
  EXPECT_CALL(*event_logger_, logEjectUnhealthy(_, _, _));
  EXPECT_CALL(*timeout_timer_, disableTimer());
  EXPECT_CALL(*interval_timer_, enableTimer(_));
  timeout_timer_->callback_();
  connection_->raiseEvent(Network::ConnectionEvent::RemoteClose);
  EXPECT_TRUE(cluster_->prioritySet().getMockHostSet(0)->hosts_[0]->healthFlagGet(
      Host::HealthFlag::FAILED_ACTIVE_HC));
  EXPECT_FALSE(cluster_->prioritySet().getMockHostSet(0)->hosts_[0]->healthy());
}

// Tests that a timeout while waiting for the server reply fails the health check
TEST_F(MySQLHealthCheckerImplTest, ServerReplyTimeoutWithoutReusingConnection) {
  InSequence s;

  setupDontReuseConnection();
  health_checker_->start();

  expectSessionCreate();
  expectClientCreate();
  cluster_->prioritySet().getMockHostSet(0)->hosts_ = {
      makeTestHost(cluster_->info_, "tcp://127.0.0.1:80")};
  EXPECT_CALL(*timeout_timer_, enableTimer(_));

  cluster_->prioritySet().getMockHostSet(0)->runCallbacks(
      {cluster_->prioritySet().getMockHostSet(0)->hosts_.back()}, {});

  connection_->raiseEvent(Network::ConnectionEvent::Connected);

  Buffer::OwnedImpl login_quit_packet;
  EXPECT_CALL(*connection_, write(BufferEqual(&build_login_quit_packet(login_quit_packet)), false))
      .Times(1);
  Buffer::OwnedImpl server_greeting_packet;
  read_filter_->onData(build_server_greeting_packet(server_greeting_packet), false);

  EXPECT_CALL(*connection_, close(_));
  EXPECT_CALL(*event_logger_, logEjectUnhealthy(_, _, _));
  EXPECT_CALL(*timeout_timer_, disableTimer());
  EXPECT_CALL(*interval_timer_, enableTimer(_));
  timeout_timer_->callback_();
  EXPECT_TRUE(cluster_->prioritySet().getMockHostSet(0)->hosts_[0]->healthFlagGet(
      Host::HealthFlag::FAILED_ACTIVE_HC));
  EXPECT_FALSE(cluster_->prioritySet().getMockHostSet(0)->hosts_[0]->healthy());
}

// Tests that a remote close while waiting for the server reply fails the health check
TEST_F(MySQLHealthCheckerImplTest, ServerReplyRemoteClose) {
  InSequence s;

  setupReuseConnection();
  health_checker_->start();

  expectSessionCreate();
  expectClientCreate();
  cluster_->prioritySet().getMockHostSet(0)->hosts_ = {
      makeTestHost(cluster_->info_, "tcp://127.0.0.1:80")};
  EXPECT_CALL(*timeout_timer_, enableTimer(_));

  cluster_->prioritySet().getMockHostSet(0)->runCallbacks(
      {cluster_->prioritySet().getMockHostSet(0)->hosts_.back()}, {});

  connection_->raiseEvent(Network::ConnectionEvent::Connected);

  Buffer::OwnedImpl login_quit_packet;
  EXPECT_CALL(*connection_, write(BufferEqual(&build_login_quit_packet(login_quit_packet)), false))
      .Times(1);
  Buffer::OwnedImpl server_greeting_packet;
  read_filter_->onData(build_server_greeting_packet(server_greeting_packet), false);
  EXPECT_CALL(*event_logger_, logEjectUnhealthy(_, _, _));
  EXPECT_CALL(*timeout_timer_, disableTimer());
  EXPECT_CALL(*interval_timer_, enableTimer(_));

  connection_->raiseEvent(Network::ConnectionEvent::RemoteClose);
  EXPECT_TRUE(cluster_->prioritySet().getMockHostSet(0)->hosts_[0]->healthFlagGet(
      Host::HealthFlag::FAILED_ACTIVE_HC));
  EXPECT_FALSE(cluster_->prioritySet().getMockHostSet(0)->hosts_[0]->healthy());
}

// Tests that a remote close while waiting for the server reply fails the health check
TEST_F(MySQLHealthCheckerImplTest, ServerReplyRemoteCloseWithoutReusingConnection) {
  InSequence s;

  setupDontReuseConnection();
  health_checker_->start();

  expectSessionCreate();
  expectClientCreate();
  cluster_->prioritySet().getMockHostSet(0)->hosts_ = {
      makeTestHost(cluster_->info_, "tcp://127.0.0.1:80")};
  EXPECT_CALL(*timeout_timer_, enableTimer(_));

  cluster_->prioritySet().getMockHostSet(0)->runCallbacks(
      {cluster_->prioritySet().getMockHostSet(0)->hosts_.back()}, {});

  connection_->raiseEvent(Network::ConnectionEvent::Connected);

  Buffer::OwnedImpl login_quit_packet;
  EXPECT_CALL(*connection_, write(BufferEqual(&build_login_quit_packet(login_quit_packet)), false))
      .Times(1);
  Buffer::OwnedImpl server_greeting_packet;
  read_filter_->onData(build_server_greeting_packet(server_greeting_packet), false);
  EXPECT_CALL(*event_logger_, logEjectUnhealthy(_, _, _));
  EXPECT_CALL(*timeout_timer_, disableTimer());
  EXPECT_CALL(*interval_timer_, enableTimer(_));

  connection_->raiseEvent(Network::ConnectionEvent::RemoteClose);
  EXPECT_TRUE(cluster_->prioritySet().getMockHostSet(0)->hosts_[0]->healthFlagGet(
      Host::HealthFlag::FAILED_ACTIVE_HC));
  EXPECT_FALSE(cluster_->prioritySet().getMockHostSet(0)->hosts_[0]->healthy());
}

} // namespace MySQLHealthChecker
} // namespace HealthCheckers
} // namespace Extensions
} // namespace Envoy
