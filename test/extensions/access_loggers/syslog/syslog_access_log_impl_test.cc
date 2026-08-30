#include "source/common/api/api_impl.h"
#include "source/common/network/address_impl.h"
#include "source/common/network/io_socket_error_impl.h"
#include "source/common/network/socket_impl.h"
#include "source/extensions/access_loggers/syslog/syslog_access_log_impl.h"
#include "source/extensions/access_loggers/syslog/udp_sender.h"

#include "test/mocks/stream_info/mocks.h"
#include "test/mocks/upstream/cluster_manager.h"
#include "test/test_common/environment.h"
#include "test/test_common/utility.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace AccessLoggers {
namespace Syslog {
namespace {

using testing::StartsWith;

const std::string TestAccessLog = R"([2026-08-19T14:20:54.123Z] "GET / HTTP/1.1" 200 12 1ms)";
constexpr int64_t TestTimestampSeconds = 1787149254;

TEST(SyslogStatsTest, DefinesMetricNamesAndMessageStateTags) {
  Stats::IsolatedStoreImpl store;
  SyslogAccessLogStats default_stats(*store.rootScope(), "default");
  SyslogAccessLogStats stats(*store.rootScope(), "audit");

  EXPECT_EQ("access_logs.syslog.default.bytes_sent", default_stats.bytes_sent_.name());
  EXPECT_EQ("access_logs.syslog.default.bytes_truncated", default_stats.bytes_truncated_.name());
  EXPECT_EQ("access_logs.syslog.default.send", default_stats.send_.name());

  EXPECT_EQ("access_logs.syslog.audit.messages.state.full", stats.messages_full_.name());
  EXPECT_EQ("access_logs.syslog.audit.messages", stats.messages_full_.tagExtractedName());
  ASSERT_EQ(1, stats.messages_full_.tags().size());
  EXPECT_EQ("state", stats.messages_full_.tags()[0].name_);
  EXPECT_EQ("full", stats.messages_full_.tags()[0].value_);

  EXPECT_EQ("access_logs.syslog.audit.messages.state.truncated", stats.messages_truncated_.name());
  EXPECT_EQ("access_logs.syslog.audit.messages", stats.messages_truncated_.tagExtractedName());
  ASSERT_EQ(1, stats.messages_truncated_.tags().size());
  EXPECT_EQ("state", stats.messages_truncated_.tags()[0].name_);
  EXPECT_EQ("truncated", stats.messages_truncated_.tags()[0].value_);
}

TEST(SyslogStatsTest, AccountsWriteResults) {
  Stats::IsolatedStoreImpl store;
  SyslogAccessLogStats stats(*store.rootScope(), "audit");

  accountWriteResult({7, Api::IoError::none()}, stats);
  accountWriteResult({0, Network::IoSocketError::getIoSocketEagainError()}, stats);
  accountWriteResult({0, Network::IoSocketError::create(SOCKET_ERROR_INVAL)}, stats);

  EXPECT_EQ(1, stats.send_.value());
  EXPECT_EQ(7, stats.bytes_sent_.value());
}

class TestBodyFormatter : public Formatter::Formatter {
public:
  explicit TestBodyFormatter(const std::string& body) : body_(body) {}

  std::string format(const Envoy::Formatter::Context&,
                     const StreamInfo::StreamInfo&) const override {
    return body_;
  }

  void formatTo(std::string& output, const Envoy::Formatter::Context&,
                const StreamInfo::StreamInfo&) const override {
    output.append(body_);
  }

private:
  const std::string& body_;
};

class FakeSender : public Sender {
public:
  void send(absl::string_view message) override {
    send_count_++;
    message_.assign(message.data(), message.size());
  }

  std::string message_;
  uint64_t send_count_{0};
};

class FailingSender : public Sender {
public:
  void send(absl::string_view) override {}
};

class SyslogAccessLoggerImplTest : public testing::Test {
protected:
  SyslogAccessLoggerImplTest()
      : stats_(*store_.rootScope(), "test"), body_(TestAccessLog),
        body_formatter_(std::make_shared<TestBodyFormatter>(body_)) {
    config_.set_no_hostname(true);
    config_.set_stat_prefix("test");
    sender_ = new FakeSender();
    logger_ = std::make_unique<SyslogAccessLoggerImpl>(config_, body_formatter_, SenderPtr(sender_),
                                                       stats_);
  }

  void log() { logger_->log(Formatter::Context{}, stream_info_); }

  void recreateLogger() {
    sender_ = new FakeSender();
    logger_ = std::make_unique<SyslogAccessLoggerImpl>(config_, body_formatter_, SenderPtr(sender_),
                                                       stats_);
  }

  Stats::IsolatedStoreImpl store_;
  SyslogAccessLogStats stats_;
  std::string body_;
  const Formatter::FormatterConstSharedPtr body_formatter_;
  SyslogAccessLogConfig config_;
  FakeSender* sender_;
  std::unique_ptr<SyslogAccessLoggerImpl> logger_;
  testing::NiceMock<StreamInfo::MockStreamInfo> stream_info_;
};

TEST_F(SyslogAccessLoggerImplTest, FormatsAndSendsRfc3164Message) {
  stream_info_.ts_.setSystemTime(std::chrono::system_clock::from_time_t(TestTimestampSeconds));
  log();

  EXPECT_EQ(R"(<190>Aug 19 14:20:54 envoy: [2026-08-19T14:20:54.123Z] "GET / HTTP/1.1" 200 12 1ms)",
            sender_->message_);
  EXPECT_EQ(1, stats_.messages_full_.value());
}

TEST_F(SyslogAccessLoggerImplTest, FormatsAndSendsRfc5424Message) {
  config_.set_syslog_format(SyslogAccessLogConfig::RFC5424);
  recreateLogger();
  stream_info_.ts_.setSystemTime(std::chrono::system_clock::from_time_t(TestTimestampSeconds));
  log();

  EXPECT_THAT(sender_->message_,
              testing::EndsWith(absl::StrCat(" envoy.access - ", TestAccessLog)));

  config_.set_msg_id("envoy.audit");
  recreateLogger();
  log();

  EXPECT_THAT(sender_->message_, testing::EndsWith(absl::StrCat(" envoy.audit - ", TestAccessLog)));
}

TEST_F(SyslogAccessLoggerImplTest, MapsRfc3164Priorities) {
  struct PriorityTestCase {
    SyslogAccessLogConfig::Facility facility;
    SyslogAccessLogConfig::Severity severity;
    absl::string_view priority;
  };
  const std::vector<PriorityTestCase> test_cases = {
      {SyslogAccessLogConfig::FACILITY_KERN, SyslogAccessLogConfig::EMERG, "<0>"},
      {SyslogAccessLogConfig::FACILITY_USER, SyslogAccessLogConfig::INFO, "<14>"},
      {SyslogAccessLogConfig::FACILITY_LOCAL0, SyslogAccessLogConfig::DEBUG, "<135>"},
      {SyslogAccessLogConfig::FACILITY_LOCAL7, SyslogAccessLogConfig::INFO, "<190>"},
      {SyslogAccessLogConfig::FACILITY_LOCAL7, SyslogAccessLogConfig::DEBUG, "<191>"},
  };

  for (const auto& test_case : test_cases) {
    config_.set_facility(test_case.facility);
    config_.set_severity(test_case.severity);
    config_.set_tag(test_case.priority == "<135>" ? "edge" : "");
    recreateLogger();
    log();
    EXPECT_THAT(sender_->message_, StartsWith(test_case.priority));
    if (test_case.priority == "<135>") {
      EXPECT_THAT(sender_->message_, testing::EndsWith(absl::StrCat(" edge: ", TestAccessLog)));
    }
  }
}

TEST_F(SyslogAccessLoggerImplTest, PreservesEmbeddedNullInAccessLog) {
  const std::string access_log("a\0b", 3);
  body_ = access_log;
  log();

  ASSERT_GE(sender_->message_.size(), access_log.size());
  EXPECT_EQ(access_log, sender_->message_.substr(sender_->message_.size() - access_log.size()));
  EXPECT_EQ(1, sender_->send_count_);
}

TEST_F(SyslogAccessLoggerImplTest, EnforcesMaxMessageSize) {
  config_.set_max_syslog_msg_bytes(480);
  recreateLogger();
  body_.clear();
  log();
  const size_t header_size = sender_->message_.size();
  ASSERT_LT(header_size, 480);

  body_ = std::string(480 - header_size, 'a');
  log();
  EXPECT_EQ(480, sender_->message_.size());
  EXPECT_EQ(2, stats_.messages_full_.value());

  body_ = std::string(481 - header_size, 'a');
  log();
  EXPECT_EQ(480, sender_->message_.size());
  EXPECT_EQ(1, stats_.messages_truncated_.value());
  EXPECT_EQ(2, stats_.messages_full_.value());
  EXPECT_EQ(1, stats_.bytes_truncated_.value());
}

TEST_F(SyslogAccessLoggerImplTest, CountsTruncatedMessageWhenSendFails) {
  config_.set_max_syslog_msg_bytes(1);
  logger_ = std::make_unique<SyslogAccessLoggerImpl>(config_, body_formatter_,
                                                     std::make_unique<FailingSender>(), stats_);

  log();

  EXPECT_EQ(1, stats_.messages_truncated_.value());
  EXPECT_GT(stats_.bytes_truncated_.value(), 0);
  EXPECT_EQ(0, stats_.bytes_sent_.value());
  EXPECT_EQ(0, stats_.send_.value());
}

class SenderTestBase : public testing::Test {
protected:
  SenderTestBase()
      : api_(Api::createApiForTest()), dispatcher_(api_->allocateDispatcher("syslog_test")),
        stats_(*store_.rootScope(), "test") {}

  void expectDatagram(Network::Address::InstanceConstSharedPtr address) {
    Network::SocketImpl receiver(Network::Socket::Type::Datagram, address, nullptr,
                                 Network::SocketCreationOptions{});
    ASSERT_EQ(0, receiver.bind(address).return_value_);
    const auto destination = address->type() == Network::Address::Type::Pipe
                                 ? address
                                 : receiver.connectionInfoProvider().localAddress();
    StaticUdpSender sender(*dispatcher_, destination, stats_);
    sender.send("message");

    char data[32]{};
    const Api::IoCallUint64Result result = receiver.ioHandle().recv(data, sizeof(data), 0);
    ASSERT_TRUE(result.ok());
    EXPECT_EQ("message", absl::string_view(data, result.return_value_));
    EXPECT_EQ(1, stats_.send_.value());
    EXPECT_EQ(sizeof("message") - 1, stats_.bytes_sent_.value());
  }

  Api::ApiPtr api_;
  Event::DispatcherPtr dispatcher_;
  Stats::IsolatedStoreImpl store_;
  SyslogAccessLogStats stats_;
};

class StaticUdpSenderTest : public SenderTestBase {};

TEST_F(StaticUdpSenderTest, RejectsNullDestination) {
  EXPECT_DEATH(StaticUdpSender(*dispatcher_, nullptr, stats_),
               "Syslog UDP destination must not be null");
}

TEST_F(StaticUdpSenderTest, SendsIpv4UdpDatagram) {
  expectDatagram(std::make_shared<Network::Address::Ipv4Instance>("127.0.0.1", 0));
}

TEST_F(StaticUdpSenderTest, SendsIpv6UdpDatagram) {
  if (!TestEnvironment::shouldRunTestForIpVersion(Network::Address::IpVersion::v6)) {
    GTEST_SKIP();
  }
  expectDatagram(std::make_shared<Network::Address::Ipv6Instance>("::1", 0));
}

#ifndef WIN32
TEST_F(StaticUdpSenderTest, SendsUnixDomainDatagram) {
#ifdef __linux__
  expectDatagram(
      *Network::Address::PipeInstance::create(absl::StrCat("@", TestUtility::uniqueFilename())));
#else
  expectDatagram(*Network::Address::PipeInstance::create(
      TestEnvironment::temporaryPath(TestUtility::uniqueFilename())));
#endif
}

TEST_F(StaticUdpSenderTest, CountsDatagramSendError) {
#ifdef __linux__
  const std::string path = absl::StrCat("@", TestUtility::uniqueFilename());
#else
  const std::string path = absl::StrCat("/tmp/", TestUtility::uniqueFilename());
#endif
  Network::Address::InstanceConstSharedPtr destination =
      Network::Address::PipeInstance::create(path).value();
  StaticUdpSender sender(*dispatcher_, destination, stats_);

  sender.send("message");

  EXPECT_EQ(0, stats_.bytes_sent_.value());
  EXPECT_EQ(0, stats_.send_.value());
}
#endif

class ClusterUdpSenderTest : public SenderTestBase {};

TEST_F(ClusterUdpSenderTest, IgnoresUnavailableDestinations) {
  Upstream::MockClusterManager cluster_manager;
  EXPECT_CALL(cluster_manager, getThreadLocalCluster("syslog_cluster"))
      .WillOnce(testing::Return(nullptr))
      .WillRepeatedly(testing::Return(&cluster_manager.thread_local_cluster_));
  auto host = std::make_shared<testing::NiceMock<Upstream::MockHost>>();
  Network::Address::InstanceConstSharedPtr pipe =
      Network::Address::PipeInstance::create("/syslog.sock").value();
  ON_CALL(*host, address()).WillByDefault(testing::Return(pipe));
  EXPECT_CALL(cluster_manager.thread_local_cluster_.lb_, chooseHost(nullptr))
      .WillOnce(testing::Return(Upstream::HostSelectionResponse{nullptr}))
      .WillOnce(testing::Return(Upstream::HostSelectionResponse{host}));
  ClusterUdpSender sender(*dispatcher_, cluster_manager, "syslog_cluster", stats_);

  sender.send("message");
  sender.send("message");
  sender.send("message");

  EXPECT_EQ(0, stats_.bytes_sent_.value());
  EXPECT_EQ(0, stats_.send_.value());
}

TEST_F(ClusterUdpSenderTest, SelectsHostForEveryRecord) {
  auto bind_address = std::make_shared<Network::Address::Ipv4Instance>("127.0.0.1", 0);
  Network::SocketImpl first_receiver(Network::Socket::Type::Datagram, bind_address, nullptr,
                                     Network::SocketCreationOptions{});
  Network::SocketImpl second_receiver(Network::Socket::Type::Datagram, bind_address, nullptr,
                                      Network::SocketCreationOptions{});
  ASSERT_EQ(0, first_receiver.bind(bind_address).return_value_);
  ASSERT_EQ(0, second_receiver.bind(bind_address).return_value_);

  auto first_host = std::make_shared<testing::NiceMock<Upstream::MockHost>>();
  auto second_host = std::make_shared<testing::NiceMock<Upstream::MockHost>>();
  ON_CALL(*first_host, address())
      .WillByDefault(testing::Return(first_receiver.connectionInfoProvider().localAddress()));
  ON_CALL(*second_host, address())
      .WillByDefault(testing::Return(second_receiver.connectionInfoProvider().localAddress()));
  Upstream::MockClusterManager cluster_manager;
  EXPECT_CALL(cluster_manager, getThreadLocalCluster("syslog_cluster"))
      .Times(2)
      .WillRepeatedly(testing::Return(&cluster_manager.thread_local_cluster_));
  EXPECT_CALL(cluster_manager.thread_local_cluster_.lb_, chooseHost(nullptr))
      .WillOnce(testing::Return(Upstream::HostSelectionResponse{first_host}))
      .WillOnce(testing::Return(Upstream::HostSelectionResponse{second_host}));
  ClusterUdpSender sender(*dispatcher_, cluster_manager, "syslog_cluster", stats_);

  sender.send("message");
  sender.send("message");

  char data[32]{};
  Api::IoCallUint64Result result = first_receiver.ioHandle().recv(data, sizeof(data), 0);
  ASSERT_TRUE(result.ok());
  EXPECT_EQ("message", absl::string_view(data, result.return_value_));
  result = second_receiver.ioHandle().recv(data, sizeof(data), 0);
  ASSERT_TRUE(result.ok());
  EXPECT_EQ("message", absl::string_view(data, result.return_value_));
  EXPECT_EQ(2, stats_.send_.value());
  EXPECT_EQ(2 * (sizeof("message") - 1), stats_.bytes_sent_.value());
}

} // namespace
} // namespace Syslog
} // namespace AccessLoggers
} // namespace Extensions
} // namespace Envoy
