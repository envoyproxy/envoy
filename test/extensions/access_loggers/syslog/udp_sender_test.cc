#include "source/common/api/api_impl.h"
#include "source/common/network/address_impl.h"
#include "source/common/network/io_socket_error_impl.h"
#include "source/extensions/access_loggers/syslog/stats.h"
#include "source/extensions/access_loggers/syslog/udp_sender.h"

#include "test/mocks/api/mocks.h"
#include "test/mocks/upstream/cluster_manager.h"
#include "test/mocks/upstream/host.h"
#include "test/test_common/environment.h"
#include "test/test_common/network_utility.h"
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

class ClusterUdpSenderIpTest : public ClusterUdpSenderTest,
                               public testing::WithParamInterface<Network::Address::IpVersion> {};

INSTANTIATE_TEST_SUITE_P(IpVersions, ClusterUdpSenderIpTest,
                         testing::ValuesIn(TestEnvironment::getIpVersionsForTest()),
                         TestUtility::ipTestParamsToString);

TEST_F(ClusterUdpSenderTest, IgnoresUnavailableCluster) {
  Upstream::MockClusterManager cluster_manager;
  EXPECT_CALL(cluster_manager, getThreadLocalCluster("syslog_cluster"))
      .WillOnce(testing::Return(nullptr));
  ClusterUdpSender sender(*dispatcher_, cluster_manager, "syslog_cluster", stats_);

  sender.send("message");

  EXPECT_EQ(0, counterValue(store_, "bytes_sent"));
  EXPECT_EQ(0, counterValue(store_, "send"));
}

TEST_F(ClusterUdpSenderTest, IgnoresUnavailableHost) {
  Upstream::MockClusterManager cluster_manager;
  EXPECT_CALL(cluster_manager, getThreadLocalCluster("syslog_cluster"))
      .WillOnce(testing::Return(&cluster_manager.thread_local_cluster_));
  EXPECT_CALL(cluster_manager.thread_local_cluster_.lb_, chooseHost(nullptr))
      .WillOnce(testing::Return(Upstream::HostSelectionResponse{nullptr}));
  ClusterUdpSender sender(*dispatcher_, cluster_manager, "syslog_cluster", stats_);

  sender.send("message");

  EXPECT_EQ(0, counterValue(store_, "bytes_sent"));
  EXPECT_EQ(0, counterValue(store_, "send"));
}

TEST_F(ClusterUdpSenderTest, IgnoresNonIpHost) {
  Upstream::MockClusterManager cluster_manager;
  EXPECT_CALL(cluster_manager, getThreadLocalCluster("syslog_cluster"))
      .WillOnce(testing::Return(&cluster_manager.thread_local_cluster_));
  auto host = std::make_shared<testing::NiceMock<Upstream::MockHost>>();
  Network::Address::InstanceConstSharedPtr pipe =
      Network::Address::PipeInstance::create("/syslog.sock").value();
  ON_CALL(*host, address()).WillByDefault(testing::Return(pipe));
  EXPECT_CALL(cluster_manager.thread_local_cluster_.lb_, chooseHost(nullptr))
      .WillOnce(testing::Return(Upstream::HostSelectionResponse{host}));
  ClusterUdpSender sender(*dispatcher_, cluster_manager, "syslog_cluster", stats_);

  sender.send("message");

  EXPECT_EQ(0, counterValue(store_, "bytes_sent"));
  EXPECT_EQ(0, counterValue(store_, "send"));
}

TEST_P(ClusterUdpSenderIpTest, SendsToIpHost) {
  Network::Test::UdpSyncPeer receiver(GetParam());
  auto host = std::make_shared<testing::NiceMock<Upstream::MockHost>>();
  ON_CALL(*host, address()).WillByDefault(testing::Return(receiver.localAddress()));
  Upstream::MockClusterManager cluster_manager;
  EXPECT_CALL(cluster_manager, getThreadLocalCluster("syslog_cluster"))
      .WillOnce(testing::Return(&cluster_manager.thread_local_cluster_));
  EXPECT_CALL(cluster_manager.thread_local_cluster_.lb_, chooseHost(nullptr))
      .WillOnce(testing::Return(Upstream::HostSelectionResponse{host}));
  ClusterUdpSender sender(*dispatcher_, cluster_manager, "syslog_cluster", stats_);

  sender.send("message");

  Network::UdpRecvData data;
  receiver.recv(data);
  EXPECT_EQ("message", data.buffer_->toString());
  EXPECT_EQ(1, counterValue(store_, "send"));
  EXPECT_EQ(sizeof("message") - 1, counterValue(store_, "bytes_sent"));
}

TEST_F(ClusterUdpSenderTest, SelectsHostForEveryRecord) {
  Network::Test::UdpSyncPeer first_receiver(Network::Address::IpVersion::v4);
  Network::Test::UdpSyncPeer second_receiver(Network::Address::IpVersion::v4);

  auto first_host = std::make_shared<testing::NiceMock<Upstream::MockHost>>();
  auto second_host = std::make_shared<testing::NiceMock<Upstream::MockHost>>();
  ON_CALL(*first_host, address()).WillByDefault(testing::Return(first_receiver.localAddress()));
  ON_CALL(*second_host, address()).WillByDefault(testing::Return(second_receiver.localAddress()));
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

  Network::UdpRecvData data;
  first_receiver.recv(data);
  EXPECT_EQ("message", data.buffer_->toString());
  second_receiver.recv(data);
  EXPECT_EQ("message", data.buffer_->toString());
  EXPECT_EQ(2, counterValue(store_, "send"));
  EXPECT_EQ(2 * (sizeof("message") - 1), counterValue(store_, "bytes_sent"));
}

} // namespace
} // namespace Syslog
} // namespace AccessLoggers
} // namespace Extensions
} // namespace Envoy
