#include "source/common/api/api_impl.h"
#include "source/common/network/address_impl.h"
#include "source/common/network/io_socket_error_impl.h"
#include "source/common/network/socket_impl.h"
#include "source/extensions/access_loggers/syslog/syslog_access_log_impl.h"
#include "source/extensions/access_loggers/syslog/udp_sender.h"

#include "test/mocks/api/mocks.h"
#include "test/mocks/stream_info/mocks.h"
#include "test/mocks/upstream/cluster_manager.h"
#include "test/mocks/upstream/host.h"
#include "test/test_common/environment.h"
#include "test/test_common/threadsafe_singleton_injector.h"
#include "test/test_common/utility.h"

#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace AccessLoggers {
namespace Syslog {
namespace {

uint64_t counterValue(Stats::IsolatedStoreImpl& store, absl::string_view name) {
  const auto counter =
      TestUtility::findCounter(store, absl::StrCat("access_logs.syslog.test.", name));
  return counter != nullptr ? counter->value() : 0;
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

TEST(SyslogAccessLoggerImplTest, PreservesEmbeddedNullInAccessLog) {
  Stats::IsolatedStoreImpl store;
  SyslogAccessLogStats stats(*store.rootScope(), "test");
  SyslogAccessLogConfig config;
  config.set_no_hostname(true);
  const std::string access_log("a\0b", 3);
  auto sender = std::make_unique<FakeSender>();
  FakeSender* sender_ptr = sender.get();
  SyslogAccessLoggerImpl logger(config, std::make_shared<TestBodyFormatter>(access_log),
                                std::move(sender), stats);

  logger.log(Formatter::Context{}, testing::NiceMock<StreamInfo::MockStreamInfo>());

  ASSERT_GE(sender_ptr->message_.size(), access_log.size());
  EXPECT_EQ(access_log,
            sender_ptr->message_.substr(sender_ptr->message_.size() - access_log.size()));
  EXPECT_EQ(1, sender_ptr->send_count_);
}

TEST(SyslogAccessLoggerImplTest, CountsTruncatedMessageWhenSendFails) {
  Stats::IsolatedStoreImpl store;
  SyslogAccessLogStats stats(*store.rootScope(), "test");
  SyslogAccessLogConfig config;
  config.set_no_hostname(true);
  config.set_max_syslog_msg_bytes(1);
  const std::string body("payload");
  SyslogAccessLoggerImpl logger(config, std::make_shared<TestBodyFormatter>(body),
                                std::make_unique<FailingSender>(), stats);

  logger.log(Formatter::Context{}, testing::NiceMock<StreamInfo::MockStreamInfo>());

  EXPECT_EQ(1, counterValue(store, "messages.state.truncated"));
  EXPECT_GT(counterValue(store, "bytes_truncated"), 0);
  EXPECT_EQ(0, counterValue(store, "bytes_sent"));
  EXPECT_EQ(0, counterValue(store, "send"));
}

class SenderTestBase : public testing::Test {
protected:
  SenderTestBase()
      : api_(Api::createApiForTest()), dispatcher_(api_->allocateDispatcher("syslog_test")),
        stats_(*store_.rootScope(), "test") {}

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

#ifndef WIN32
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

  EXPECT_EQ(0, counterValue(store_, "bytes_sent"));
  EXPECT_EQ(0, counterValue(store_, "send"));
}
#endif

TEST_F(StaticUdpSenderTest, RecoversAfterWriteBecomesWritable) {
  auto destination = std::make_shared<Network::Address::Ipv4Instance>("127.0.0.1", 0);
  StaticUdpSender sender(*dispatcher_, destination, stats_);
  Api::MockOsSysCalls os_sys_calls;
  TestThreadsafeSingletonInjector<Api::OsSysCallsImpl> os_calls(&os_sys_calls);
  EXPECT_CALL(os_sys_calls, sendmsg(testing::_, testing::_, testing::_))
      .WillOnce(testing::Return(Api::SysCallSizeResult{-1, SOCKET_ERROR_AGAIN}))
      .WillOnce(testing::Return(Api::SysCallSizeResult{7, 0}));

  sender.send("message");
  sender.send("message");
  EXPECT_EQ(0, counterValue(store_, "send"));
  EXPECT_EQ(0, counterValue(store_, "bytes_sent"));

  dispatcher_->run(Event::Dispatcher::RunType::NonBlock);
  sender.send("message");

  EXPECT_EQ(1, counterValue(store_, "send"));
  EXPECT_EQ(7, counterValue(store_, "bytes_sent"));
}

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

  EXPECT_EQ(0, counterValue(store_, "bytes_sent"));
  EXPECT_EQ(0, counterValue(store_, "send"));
}

TEST_F(ClusterUdpSenderTest, SendsToIpv4AndIpv6Hosts) {
  if (!TestEnvironment::shouldRunTestForIpVersion(Network::Address::IpVersion::v6)) {
    GTEST_SKIP();
  }

  auto v4_bind = std::make_shared<Network::Address::Ipv4Instance>("127.0.0.1", 0);
  auto v6_bind = std::make_shared<Network::Address::Ipv6Instance>("::1", 0);
  Network::SocketImpl v4_receiver(Network::Socket::Type::Datagram, v4_bind, nullptr,
                                  Network::SocketCreationOptions{});
  Network::SocketImpl v6_receiver(Network::Socket::Type::Datagram, v6_bind, nullptr,
                                  Network::SocketCreationOptions{});
  ASSERT_EQ(0, v4_receiver.bind(v4_bind).return_value_);
  ASSERT_EQ(0, v6_receiver.bind(v6_bind).return_value_);

  auto v4_host = std::make_shared<testing::NiceMock<Upstream::MockHost>>();
  auto v6_host = std::make_shared<testing::NiceMock<Upstream::MockHost>>();
  ON_CALL(*v4_host, address())
      .WillByDefault(testing::Return(v4_receiver.connectionInfoProvider().localAddress()));
  ON_CALL(*v6_host, address())
      .WillByDefault(testing::Return(v6_receiver.connectionInfoProvider().localAddress()));

  Upstream::MockClusterManager cluster_manager;
  EXPECT_CALL(cluster_manager, getThreadLocalCluster("syslog_cluster"))
      .Times(2)
      .WillRepeatedly(testing::Return(&cluster_manager.thread_local_cluster_));
  EXPECT_CALL(cluster_manager.thread_local_cluster_.lb_, chooseHost(nullptr))
      .WillOnce(testing::Return(Upstream::HostSelectionResponse{v4_host}))
      .WillOnce(testing::Return(Upstream::HostSelectionResponse{v6_host}));
  ClusterUdpSender sender(*dispatcher_, cluster_manager, "syslog_cluster", stats_);

  sender.send("v4-message");
  sender.send("v6-message");

  char data[32]{};
  Api::IoCallUint64Result result = v4_receiver.ioHandle().recv(data, sizeof(data), 0);
  ASSERT_TRUE(result.ok());
  EXPECT_EQ("v4-message", absl::string_view(data, result.return_value_));
  result = v6_receiver.ioHandle().recv(data, sizeof(data), 0);
  ASSERT_TRUE(result.ok());
  EXPECT_EQ("v6-message", absl::string_view(data, result.return_value_));
  EXPECT_EQ(2, counterValue(store_, "send"));
  EXPECT_EQ(sizeof("v4-message") + sizeof("v6-message") - 2, counterValue(store_, "bytes_sent"));
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
  EXPECT_EQ(2, counterValue(store_, "send"));
  EXPECT_EQ(2 * (sizeof("message") - 1), counterValue(store_, "bytes_sent"));
}

} // namespace
} // namespace Syslog
} // namespace AccessLoggers
} // namespace Extensions
} // namespace Envoy
