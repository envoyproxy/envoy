#include <functional>
#include <string>
#include <tuple>
#include <vector>

#include "envoy/config/accesslog/v3/accesslog.pb.h"
#include "envoy/extensions/access_loggers/syslog/v3/syslog.pb.h"

#include "source/common/access_log/access_log_impl.h"
#include "source/common/api/api_impl.h"
#include "source/common/formatter/substitution_format_utility.h"
#include "source/common/network/address_impl.h"
#include "source/common/network/socket_impl.h"

#include "test/mocks/server/server_factory_context.h"
#include "test/mocks/stream_info/mocks.h"
#include "test/mocks/upstream/host.h"
#include "test/test_common/environment.h"
#include "test/test_common/simulated_time_system.h"
#include "test/test_common/utility.h"

#include "absl/strings/str_cat.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace AccessLoggers {
namespace Syslog {
namespace {

using SyslogAccessLogConfig = envoy::extensions::access_loggers::syslog::v3::SyslogAccessLogConfig;
using testing::MatchesRegex;
using testing::Return;
using testing::StartsWith;

constexpr int64_t TestTimestampSeconds = 1787149254;

constexpr absl::string_view UdpServerConfigYaml = R"EOF(
server:
  socket_address:
    address: 127.0.0.1
    port_value: 514
    protocol: UDP
no_hostname: true
stat_prefix: test
log_format:
  text_format_source:
    inline_string: test
)EOF";

constexpr absl::string_view UdpClusterConfigYaml = R"EOF(
cluster:
  name: syslog
no_hostname: true
stat_prefix: test
log_format:
  text_format_source:
    inline_string: test
)EOF";

SyslogAccessLogConfig loadConfig(absl::string_view yaml) {
  SyslogAccessLogConfig config;
  TestUtility::loadFromYaml(std::string(yaml), config);
  return config;
}

std::string receiveDatagram(Network::SocketImpl& receiver, size_t buffer_size = 2048) {
  std::string buffer(buffer_size, '\0');
  const Api::IoCallUint64Result result = receiver.ioHandle().recv(buffer.data(), buffer.size(), 0);
  EXPECT_TRUE(result.ok());
  if (!result.ok()) {
    return {};
  }
  buffer.resize(result.return_value_);
  return buffer;
}

class SyslogAccessLogTest : public Event::TestUsingSimulatedTime, public testing::Test {
protected:
  struct MetricSnapshot {
    uint64_t send;
    uint64_t bytes_sent;
    uint64_t bytes_truncated;
    uint64_t messages_full;
    uint64_t messages_truncated;
  };

  SyslogAccessLogTest()
      : api_(Api::createApiForTest(simTime())),
        dispatcher_(api_->allocateDispatcher("syslog_test")),
        bind_address_(std::make_shared<Network::Address::Ipv4Instance>("127.0.0.1", 0)),
        receiver_(Network::Socket::Type::Datagram, bind_address_, nullptr,
                  Network::SocketCreationOptions{}),
        request_headers_{{":method", "GET"}, {":path", "/bar"}} {
    EXPECT_EQ(0, receiver_.bind(bind_address_).return_value_);
    context_.server_context_.thread_local_.setDispatcher(dispatcher_.get());
    stream_info_.setResponseCode(200);
    stream_info_.ts_.setSystemTime(std::chrono::system_clock::from_time_t(TestTimestampSeconds));
  }

  uint32_t receiverPort() const {
    return receiver_.connectionInfoProvider().localAddress()->ip()->port();
  }

  Network::Address::InstanceConstSharedPtr receiverAddress() const {
    return receiver_.connectionInfoProvider().localAddress();
  }

  SyslogAccessLogConfig serverConfig() {
    auto config = loadConfig(UdpServerConfigYaml);
    config.mutable_server()->mutable_socket_address()->set_port_value(receiverPort());
    return config;
  }

  AccessLog::InstanceSharedPtr createLogger(const SyslogAccessLogConfig& proto_config) {
    envoy::config::accesslog::v3::AccessLog config;
    config.set_name("envoy.access_loggers.syslog");
    std::ignore = config.mutable_typed_config()->PackFrom(proto_config);
    return AccessLog::AccessLogFactory::fromProto(config, context_);
  }

  uint64_t counterValue(absl::string_view name) {
    const auto counter =
        TestUtility::findCounter(context_.server_context_.store_, std::string(name));
    return counter != nullptr ? counter->value() : 0;
  }

  MetricSnapshot readMetrics(absl::string_view stat_prefix) {
    const std::string prefix = absl::StrCat("access_logs.syslog.", stat_prefix, ".");
    return MetricSnapshot{
        counterValue(absl::StrCat(prefix, "send")),
        counterValue(absl::StrCat(prefix, "bytes_sent")),
        counterValue(absl::StrCat(prefix, "bytes_truncated")),
        counterValue(absl::StrCat(prefix, "messages.state.full")),
        counterValue(absl::StrCat(prefix, "messages.state.truncated")),
    };
  }

  void logOnce(const AccessLog::InstanceSharedPtr& access_log) {
    access_log->log({&request_headers_, &response_headers_, &response_trailers_}, stream_info_);
  }

  std::string emitCheckingOutputs(const AccessLog::InstanceSharedPtr& access_log,
                                  Network::SocketImpl& receiver,
                                  absl::string_view stat_prefix = "test",
                                  uint64_t original_size = 0) {
    const MetricSnapshot before = readMetrics(stat_prefix);
    logOnce(access_log);
    const std::string datagram = receiveDatagram(receiver);
    const uint64_t formatted_size = original_size == 0 ? datagram.size() : original_size;
    const MetricSnapshot after = readMetrics(stat_prefix);

    EXPECT_EQ(before.send + 1, after.send);
    EXPECT_EQ(before.bytes_sent + datagram.size(), after.bytes_sent);
    if (formatted_size > datagram.size()) {
      EXPECT_EQ(before.messages_truncated + 1, after.messages_truncated);
      EXPECT_EQ(before.messages_full, after.messages_full);
      EXPECT_EQ(before.bytes_truncated + (formatted_size - datagram.size()), after.bytes_truncated);
    } else {
      EXPECT_EQ(before.messages_full + 1, after.messages_full);
      EXPECT_EQ(before.messages_truncated, after.messages_truncated);
      EXPECT_EQ(before.bytes_truncated, after.bytes_truncated);
    }
    return datagram;
  }

  std::string emitCheckingOutputs(const AccessLog::InstanceSharedPtr& access_log,
                                  absl::string_view stat_prefix = "test",
                                  uint64_t original_size = 0) {
    return emitCheckingOutputs(access_log, receiver_, stat_prefix, original_size);
  }

  Api::ApiPtr api_;
  Event::DispatcherPtr dispatcher_;
  Network::Address::InstanceConstSharedPtr bind_address_;
  Network::SocketImpl receiver_;
  testing::NiceMock<Server::Configuration::MockGenericFactoryContext> context_;
  Http::TestRequestHeaderMapImpl request_headers_;
  Http::TestResponseHeaderMapImpl response_headers_;
  Http::TestResponseTrailerMapImpl response_trailers_;
  testing::NiceMock<StreamInfo::MockStreamInfo> stream_info_;
};

TEST_F(SyslogAccessLogTest, PayloadFieldsChangeReceivedDatagram) {
  constexpr absl::string_view rfc3164_default = "<190>Aug 19 14:20:54 envoy: test";
  constexpr absl::string_view rfc5424_default =
      R"(<190>1 2026-08-19T14:20:54\.000000Z - envoy [0-9]+ envoy\.access - test)";

  struct Case {
    const char* field;
    std::function<void(SyslogAccessLogConfig&)> mutate;
    std::function<void(const std::string&)> verify;
  };

  const std::vector<Case> cases = {
      {"defaults", [](SyslogAccessLogConfig&) {},
       [&](const std::string& datagram) { EXPECT_EQ(rfc3164_default, datagram); }},
      {"facility",
       [](SyslogAccessLogConfig& config) {
         config.set_facility(SyslogAccessLogConfig::FACILITY_USER);
       },
       [](const std::string& datagram) { EXPECT_EQ("<14>Aug 19 14:20:54 envoy: test", datagram); }},
      {"severity",
       [](SyslogAccessLogConfig& config) { config.set_severity(SyslogAccessLogConfig::DEBUG); },
       [](const std::string& datagram) {
         EXPECT_EQ("<191>Aug 19 14:20:54 envoy: test", datagram);
       }},
      {"tag", [](SyslogAccessLogConfig& config) { config.set_tag("edge"); },
       [](const std::string& datagram) { EXPECT_EQ("<190>Aug 19 14:20:54 edge: test", datagram); }},
      {"no_hostname=false", [](SyslogAccessLogConfig& config) { config.set_no_hostname(false); },
       [](const std::string& datagram) {
         const auto hostname = Formatter::SubstitutionFormatUtils::getHostname();
         ASSERT_TRUE(hostname.has_value());
         ASSERT_FALSE(hostname->empty());
         EXPECT_EQ(absl::StrCat("<190>Aug 19 14:20:54 ", *hostname, " envoy: test"), datagram);
       }},
      {"log_format",
       [](SyslogAccessLogConfig& config) {
         config.mutable_log_format()->mutable_text_format_source()->set_inline_string("payload");
       },
       [](const std::string& datagram) {
         EXPECT_EQ("<190>Aug 19 14:20:54 envoy: payload", datagram);
       }},
      {"syslog_format=RFC5424",
       [](SyslogAccessLogConfig& config) {
         config.set_syslog_format(SyslogAccessLogConfig::RFC5424);
       },
       [&](const std::string& datagram) {
         EXPECT_THAT(datagram, MatchesRegex(std::string(rfc5424_default)));
       }},
      {"msg_id with RFC5424",
       [](SyslogAccessLogConfig& config) {
         config.set_syslog_format(SyslogAccessLogConfig::RFC5424);
         config.set_msg_id("envoy.audit");
       },
       [](const std::string& datagram) {
         EXPECT_THAT(
             datagram,
             MatchesRegex(
                 R"(<190>1 2026-08-19T14:20:54\.000000Z - envoy [0-9]+ envoy\.audit - test)"));
       }},
      {"msg_id ignored for RFC3164",
       [](SyslogAccessLogConfig& config) { config.set_msg_id("envoy.audit"); },
       [&](const std::string& datagram) { EXPECT_EQ(rfc3164_default, datagram); }},
      {"tag with RFC5424 APP-NAME",
       [](SyslogAccessLogConfig& config) {
         config.set_syslog_format(SyslogAccessLogConfig::RFC5424);
         config.set_tag("edge");
       },
       [](const std::string& datagram) {
         EXPECT_THAT(
             datagram,
             MatchesRegex(
                 R"(<190>1 2026-08-19T14:20:54\.000000Z - edge [0-9]+ envoy\.access - test)"));
       }},
  };

  for (const auto& test_case : cases) {
    SCOPED_TRACE(test_case.field);
    auto config = serverConfig();
    test_case.mutate(config);
    const AccessLog::InstanceSharedPtr access_log = createLogger(config);
    ASSERT_NE(nullptr, access_log);
    test_case.verify(emitCheckingOutputs(access_log));
  }
}

TEST_F(SyslogAccessLogTest, SubstitutionFormatAppearsInDatagram) {
  auto config = serverConfig();
  config.mutable_log_format()->mutable_text_format_source()->set_inline_string(
      "%REQ(:METHOD)% %REQ(:PATH)% %RESPONSE_CODE%");
  const AccessLog::InstanceSharedPtr access_log = createLogger(config);
  ASSERT_NE(nullptr, access_log);
  EXPECT_EQ("<190>Aug 19 14:20:54 envoy: GET /bar 200", emitCheckingOutputs(access_log));
}

TEST_F(SyslogAccessLogTest, MaxSyslogMsgBytesTruncatesUdpPayload) {
  constexpr absl::string_view header = "<190>Aug 19 14:20:54 envoy: ";
  const size_t header_size = header.size();

  const auto emit_with_limit = [&](uint32_t max_bytes, size_t body_size) {
    auto config = serverConfig();
    if (max_bytes != 0) {
      config.set_max_syslog_msg_bytes(max_bytes);
    }
    config.mutable_log_format()->mutable_text_format_source()->set_inline_string(
        std::string(body_size, 'x'));
    const AccessLog::InstanceSharedPtr access_log = createLogger(config);
    EXPECT_NE(nullptr, access_log);
    return emitCheckingOutputs(access_log, "test", header_size + body_size);
  };

  {
    SCOPED_TRACE("default under");
    const std::string datagram = emit_with_limit(0, 1451 - header_size);
    EXPECT_EQ(1451, datagram.size());
    EXPECT_THAT(datagram, StartsWith(header));
  }
  {
    SCOPED_TRACE("default equal");
    const std::string datagram = emit_with_limit(0, 1452 - header_size);
    EXPECT_EQ(1452, datagram.size());
    EXPECT_THAT(datagram, StartsWith(header));
  }
  {
    SCOPED_TRACE("default over");
    const std::string datagram = emit_with_limit(0, 1452 - header_size + 32);
    EXPECT_EQ(1452, datagram.size());
    EXPECT_THAT(datagram, StartsWith(header));
    EXPECT_EQ('x', datagram.back());
  }
  {
    SCOPED_TRACE("1024 under");
    const std::string datagram = emit_with_limit(1024, 1023 - header_size);
    EXPECT_EQ(1023, datagram.size());
    EXPECT_THAT(datagram, StartsWith(header));
  }
  {
    SCOPED_TRACE("1024 equal");
    const std::string datagram = emit_with_limit(1024, 1024 - header_size);
    EXPECT_EQ(1024, datagram.size());
    EXPECT_THAT(datagram, StartsWith(header));
  }
  {
    SCOPED_TRACE("1024 over");
    const std::string datagram = emit_with_limit(1024, 1024 - header_size + 32);
    EXPECT_EQ(1024, datagram.size());
    EXPECT_THAT(datagram, StartsWith(header));
    EXPECT_EQ('x', datagram.back());
  }
}

TEST_F(SyslogAccessLogTest, StatPrefixChangesEmittedStats) {
  auto config = serverConfig();
  config.set_stat_prefix("audit");
  const AccessLog::InstanceSharedPtr access_log = createLogger(config);
  ASSERT_NE(nullptr, access_log);
  EXPECT_EQ("<190>Aug 19 14:20:54 envoy: test", emitCheckingOutputs(access_log, "audit"));
  EXPECT_EQ(nullptr, TestUtility::findCounter(context_.server_context_.store_,
                                              "access_logs.syslog.test.send"));
}

TEST_F(SyslogAccessLogTest, ClusterDestinationSendsToSelectedHost) {
  auto config = loadConfig(UdpClusterConfigYaml);
  context_.server_context_.cluster_manager_.initializeClusters({"syslog"}, {});
  context_.server_context_.cluster_manager_.initializeThreadLocalClusters({"syslog"});
  EXPECT_CALL(context_.server_context_.cluster_manager_, checkActiveStaticCluster("syslog"))
      .WillOnce(Return(absl::OkStatus()));

  auto host = std::make_shared<testing::NiceMock<Upstream::MockHost>>();
  ON_CALL(*host, address()).WillByDefault(Return(receiverAddress()));
  auto& load_balancer = context_.server_context_.cluster_manager_.thread_local_cluster_.lb_;
  EXPECT_CALL(load_balancer, chooseHost(nullptr))
      .WillOnce(Return(Upstream::HostSelectionResponse{host}));

  const AccessLog::InstanceSharedPtr access_log = createLogger(config);
  ASSERT_NE(nullptr, access_log);
  EXPECT_EQ("<190>Aug 19 14:20:54 envoy: test", emitCheckingOutputs(access_log));
}

TEST_F(SyslogAccessLogTest, Ipv6ServerDestinationSendsDatagram) {
  if (!TestEnvironment::shouldRunTestForIpVersion(Network::Address::IpVersion::v6)) {
    GTEST_SKIP();
  }

  auto bind_address = std::make_shared<Network::Address::Ipv6Instance>("::1", 0);
  Network::SocketImpl receiver(Network::Socket::Type::Datagram, bind_address, nullptr,
                               Network::SocketCreationOptions{});
  ASSERT_EQ(0, receiver.bind(bind_address).return_value_);

  auto config = loadConfig(UdpServerConfigYaml);
  auto* socket_address = config.mutable_server()->mutable_socket_address();
  socket_address->set_address("::1");
  socket_address->set_port_value(receiver.connectionInfoProvider().localAddress()->ip()->port());

  const AccessLog::InstanceSharedPtr access_log = createLogger(config);
  ASSERT_NE(nullptr, access_log);
  EXPECT_EQ("<190>Aug 19 14:20:54 envoy: test", emitCheckingOutputs(access_log, receiver));
}

#ifndef WIN32
TEST_F(SyslogAccessLogTest, UnixDomainSocketDestinationSendsDatagram) {
#ifdef __linux__
  Network::Address::InstanceConstSharedPtr destination =
      Network::Address::PipeInstance::create(absl::StrCat("@", TestUtility::uniqueFilename()))
          .value();
#else
  Network::Address::InstanceConstSharedPtr destination =
      Network::Address::PipeInstance::create(
          TestEnvironment::temporaryPath(TestUtility::uniqueFilename()))
          .value();
#endif
  Network::SocketImpl receiver(Network::Socket::Type::Datagram, destination, nullptr,
                               Network::SocketCreationOptions{});
  ASSERT_EQ(0, receiver.bind(destination).return_value_);

  auto config = loadConfig(UdpServerConfigYaml);
  config.mutable_server()->mutable_pipe()->set_path(destination->asString());

  const AccessLog::InstanceSharedPtr access_log = createLogger(config);
  ASSERT_NE(nullptr, access_log);
  EXPECT_EQ("<190>Aug 19 14:20:54 envoy: test", emitCheckingOutputs(access_log, receiver));
}
#endif

} // namespace
} // namespace Syslog
} // namespace AccessLoggers
} // namespace Extensions
} // namespace Envoy
