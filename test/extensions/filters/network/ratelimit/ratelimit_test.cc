#include <memory>
#include <string>
#include <vector>

#include "envoy/extensions/filters/network/ratelimit/v3/rate_limit.pb.h"
#include "envoy/extensions/filters/network/tcp_proxy/v3/tcp_proxy.pb.h"
#include "envoy/stats/stats.h"

#include "source/common/buffer/buffer_impl.h"
#include "source/common/network/address_impl.h"
#include "source/common/network/filter_manager_impl.h"
#include "source/common/tcp_proxy/tcp_proxy.h"
#include "source/extensions/filters/network/ratelimit/ratelimit.h"
#include "source/extensions/filters/network/well_known_names.h"

#include "test/extensions/filters/common/ratelimit/mocks.h"
#include "test/mocks/network/mocks.h"
#include "test/mocks/ratelimit/mocks.h"
#include "test/mocks/runtime/mocks.h"
#include "test/mocks/server/factory_context.h"
#include "test/mocks/stream_info/mocks.h"
#include "test/mocks/tracing/mocks.h"
#include "test/test_common/printers.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::_;
using testing::InSequence;
using testing::Invoke;
using testing::NiceMock;
using testing::Return;
using testing::WithArgs;

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace RateLimitFilter {

class RateLimitFilterTest : public testing::Test {
public:
  void setUpTest(const std::string& yaml) {
    ON_CALL(runtime_.snapshot_, featureEnabled("ratelimit.tcp_filter_enabled", 100))
        .WillByDefault(Return(true));
    ON_CALL(runtime_.snapshot_, featureEnabled("ratelimit.tcp_filter_enforcing", 100))
        .WillByDefault(Return(true));

    envoy::extensions::filters::network::ratelimit::v3::RateLimit proto_config{};
    TestUtility::loadFromYaml(yaml, proto_config);
    config_ = std::make_shared<Config>(proto_config, *stats_store_.rootScope(), runtime_);
    client_ = new Filters::Common::RateLimit::MockClient();
    filter_ = std::make_unique<Filter>(config_, Filters::Common::RateLimit::ClientPtr{client_});

    auto downstream_direct_remote = Network::Address::InstanceConstSharedPtr{
        new Network::Address::Ipv4Instance("8.8.8.8", 3000)};
    filter_callbacks_.connection_.stream_info_.protocol_ = Http::Protocol::Http11;
    filter_callbacks_.connection_.stream_info_.downstream_connection_info_provider_
        ->setRemoteAddress(downstream_direct_remote);
    filter_->initializeReadFilterCallbacks(filter_callbacks_);

    // NOP currently.
    filter_->onAboveWriteBufferHighWatermark();
    filter_->onBelowWriteBufferLowWatermark();
  }

  ~RateLimitFilterTest() override {
    for (const Stats::GaugeSharedPtr& gauge : stats_store_.gauges()) {
      EXPECT_EQ(0U, gauge->value());
    }
  }

  const std::string filter_config_ = R"EOF(
domain: foo
descriptors:
- entries:
   - key: hello
     value: world
   - key: foo
     value: bar
- entries:
   - key: foo2
     value: bar2
stat_prefix: name
)EOF";

  const std::string fail_close_config_ = R"EOF(
domain: foo
descriptors:
- entries:
   - key: hello
     value: world
   - key: foo
     value: bar
- entries:
   - key: foo2
     value: bar2
stat_prefix: name
failure_mode_deny: true
)EOF";

  const std::string formatter_test_1 = R"EOF(
domain: foo
descriptors:
- entries:
  - key: remote_address
    value: "%DOWNSTREAM_REMOTE_ADDRESS_WITHOUT_PORT%"
  - key: hello
    value: "%PROTOCOL%"
stat_prefix: name
)EOF";

  const std::string formatter_test_2 = R"EOF(
domain: foo
descriptors:
- entries:
  - key: remote_address
    value: "%DOWNSTREAM_REMOTE_ADDRESS_WITHOUT_PORT%"
  - key: hello
    value: world
stat_prefix: name
)EOF";

  Stats::TestUtil::TestStore stats_store_;
  NiceMock<Runtime::MockLoader> runtime_;
  ConfigSharedPtr config_;
  Filters::Common::RateLimit::MockClient* client_;
  std::unique_ptr<Filter> filter_;
  NiceMock<Network::MockReadFilterCallbacks> filter_callbacks_;
  Filters::Common::RateLimit::RequestCallbacks* request_callbacks_{};
};

TEST_F(RateLimitFilterTest, OK) {
  InSequence s;
  setUpTest(filter_config_);

  EXPECT_CALL(*client_, limit(_, "foo",
                              testing::ContainerEq(std::vector<RateLimit::Descriptor>{
                                  {{{"hello", "world"}, {"foo", "bar"}}}, {{{"foo2", "bar2"}}}}),
                              testing::A<Tracing::Span&>(), _, 0))
      .WillOnce(
          WithArgs<0>(Invoke([&](Filters::Common::RateLimit::RequestCallbacks& callbacks) -> void {
            request_callbacks_ = &callbacks;
          })));

  EXPECT_EQ(Network::FilterStatus::StopIteration, filter_->onNewConnection());
  Buffer::OwnedImpl data("hello");
  EXPECT_EQ(Network::FilterStatus::StopIteration, filter_->onData(data, false));
  EXPECT_EQ(Network::FilterStatus::StopIteration, filter_->onData(data, false));

  EXPECT_CALL(filter_callbacks_, continueReading());
  request_callbacks_->complete(Filters::Common::RateLimit::LimitStatus::OK, nullptr, nullptr,
                               nullptr, "", nullptr);

  EXPECT_EQ(Network::FilterStatus::Continue, filter_->onData(data, false));

  EXPECT_CALL(*client_, cancel()).Times(0);
  filter_callbacks_.connection_.raiseEvent(Network::ConnectionEvent::LocalClose);

  EXPECT_EQ(1U, stats_store_.counter("ratelimit.name.total").value());
  EXPECT_EQ(1U, stats_store_.counter("ratelimit.name.ok").value());
}

TEST_F(RateLimitFilterTest, SubstitutionFormatterTest1) {
  InSequence s;
  setUpTest(formatter_test_1);

  std::vector<RateLimit::Descriptor> expected_descriptors;
  expected_descriptors.push_back({{{"remote_address", "8.8.8.8"}, {"hello", "HTTP/1.1"}}});

  std::vector<RateLimit::Descriptor> actual_descriptors =
      config_->applySubstitutionFormatter(filter_callbacks_.connection_.stream_info_);

  EXPECT_CALL(*client_, limit(_, "foo",
                              testing::ContainerEq(std::vector<RateLimit::Descriptor>{
                                  {{{"remote_address", "8.8.8.8"}, {"hello", "HTTP/1.1"}}}}),
                              testing::A<Tracing::Span&>(), _, 0))
      .WillOnce(
          WithArgs<0>(Invoke([&](Filters::Common::RateLimit::RequestCallbacks& callbacks) -> void {
            request_callbacks_ = &callbacks;
          })));

  EXPECT_THAT(expected_descriptors, testing::ContainerEq(actual_descriptors));

  EXPECT_EQ(Network::FilterStatus::StopIteration, filter_->onNewConnection());
  Buffer::OwnedImpl data("hello");
  EXPECT_EQ(Network::FilterStatus::StopIteration, filter_->onData(data, false));
  EXPECT_EQ(Network::FilterStatus::StopIteration, filter_->onData(data, false));

  EXPECT_CALL(filter_callbacks_, continueReading());
  request_callbacks_->complete(Filters::Common::RateLimit::LimitStatus::OK, nullptr, nullptr,
                               nullptr, "", nullptr);

  EXPECT_EQ(Network::FilterStatus::Continue, filter_->onData(data, false));

  EXPECT_CALL(*client_, cancel()).Times(0);
  filter_callbacks_.connection_.raiseEvent(Network::ConnectionEvent::LocalClose);

  EXPECT_EQ(1U, stats_store_.counter("ratelimit.name.total").value());
  EXPECT_EQ(1U, stats_store_.counter("ratelimit.name.ok").value());
}

TEST_F(RateLimitFilterTest, SubstitutionFormatterTest2) {
  InSequence s;
  setUpTest(formatter_test_2);

  std::vector<RateLimit::Descriptor> expected_descriptors;
  expected_descriptors.push_back({{{"remote_address", "8.8.8.8"}, {"hello", "world"}}});

  std::vector<RateLimit::Descriptor> actual_descriptors =
      config_->applySubstitutionFormatter(filter_callbacks_.connection_.stream_info_);

  for (auto it_expected = expected_descriptors.begin(), it_actual = actual_descriptors.begin();
       it_expected != expected_descriptors.end() && it_actual != actual_descriptors.end();
       ++it_expected, ++it_actual) {
    std::vector<RateLimit::DescriptorEntry> de1 = it_expected->entries_;
    std::vector<RateLimit::DescriptorEntry> de2 = it_actual->entries_;
    for (auto de_expected = de1.begin(), de_actual = de2.begin();
         de_expected != de1.end() && de_actual != de2.end(); ++de_expected, ++de_actual) {
      EXPECT_EQ(de_actual->key_, de_expected->key_);
      EXPECT_EQ(de_actual->value_, de_expected->value_);
    }
  }

  EXPECT_CALL(*client_, limit(_, "foo",
                              testing::ContainerEq(std::vector<RateLimit::Descriptor>{
                                  {{{"remote_address", "8.8.8.8"}, {"hello", "world"}}}}),
                              testing::A<Tracing::Span&>(), _, 0))
      .WillOnce(
          WithArgs<0>(Invoke([&](Filters::Common::RateLimit::RequestCallbacks& callbacks) -> void {
            request_callbacks_ = &callbacks;
          })));

  EXPECT_THAT(expected_descriptors, testing::ContainerEq(actual_descriptors));

  EXPECT_EQ(Network::FilterStatus::StopIteration, filter_->onNewConnection());
  Buffer::OwnedImpl data("hello");
  EXPECT_EQ(Network::FilterStatus::StopIteration, filter_->onData(data, false));
  EXPECT_EQ(Network::FilterStatus::StopIteration, filter_->onData(data, false));

  EXPECT_CALL(filter_callbacks_, continueReading());
  request_callbacks_->complete(Filters::Common::RateLimit::LimitStatus::OK, nullptr, nullptr,
                               nullptr, "", nullptr);

  EXPECT_EQ(Network::FilterStatus::Continue, filter_->onData(data, false));

  EXPECT_CALL(*client_, cancel()).Times(0);
  filter_callbacks_.connection_.raiseEvent(Network::ConnectionEvent::LocalClose);

  EXPECT_EQ(1U, stats_store_.counter("ratelimit.name.total").value());
  EXPECT_EQ(1U, stats_store_.counter("ratelimit.name.ok").value());
}

TEST_F(RateLimitFilterTest, OverLimit) {
  InSequence s;
  setUpTest(filter_config_);

  EXPECT_CALL(*client_, limit(_, "foo", _, _, _, 0))
      .WillOnce(
          WithArgs<0>(Invoke([&](Filters::Common::RateLimit::RequestCallbacks& callbacks) -> void {
            request_callbacks_ = &callbacks;
          })));

  EXPECT_EQ(Network::FilterStatus::StopIteration, filter_->onNewConnection());
  Buffer::OwnedImpl data("hello");
  EXPECT_EQ(Network::FilterStatus::StopIteration, filter_->onData(data, false));

  EXPECT_CALL(filter_callbacks_.connection_, close(Network::ConnectionCloseType::NoFlush, _));
  EXPECT_CALL(*client_, cancel()).Times(0);
  request_callbacks_->complete(Filters::Common::RateLimit::LimitStatus::OverLimit, nullptr, nullptr,
                               nullptr, "", nullptr);

  EXPECT_EQ(Network::FilterStatus::Continue, filter_->onData(data, false));

  EXPECT_EQ(1U, stats_store_.counter("ratelimit.name.total").value());
  EXPECT_EQ(1U, stats_store_.counter("ratelimit.name.over_limit").value());
  EXPECT_EQ(1U, stats_store_.counter("ratelimit.name.cx_closed").value());
}

TEST_F(RateLimitFilterTest, OverLimitWithDynamicMetadata) {
  InSequence s;
  setUpTest(filter_config_);

  EXPECT_CALL(*client_, limit(_, "foo", _, _, _, 0))
      .WillOnce(
          WithArgs<0>(Invoke([&](Filters::Common::RateLimit::RequestCallbacks& callbacks) -> void {
            request_callbacks_ = &callbacks;
          })));

  EXPECT_EQ(Network::FilterStatus::StopIteration, filter_->onNewConnection());
  Buffer::OwnedImpl data("hello");
  EXPECT_EQ(Network::FilterStatus::StopIteration, filter_->onData(data, false));

  Filters::Common::RateLimit::DynamicMetadataPtr dynamic_metadata =
      std::make_unique<ProtobufWkt::Struct>();
  auto* fields = dynamic_metadata->mutable_fields();
  (*fields)["name"] = ValueUtil::stringValue("my-limit");
  (*fields)["x"] = ValueUtil::numberValue(3);
  NiceMock<StreamInfo::MockStreamInfo> stream_info;
  EXPECT_CALL(filter_callbacks_.connection_, streamInfo()).WillOnce(ReturnRef(stream_info));
  EXPECT_CALL(stream_info, setDynamicMetadata(_, _))
      .WillOnce(Invoke([&dynamic_metadata](const std::string& ns,
                                           const ProtobufWkt::Struct& returned_dynamic_metadata) {
        EXPECT_EQ(ns, NetworkFilterNames::get().RateLimit);
        EXPECT_TRUE(TestUtility::protoEqual(returned_dynamic_metadata, *dynamic_metadata));
      }));

  EXPECT_CALL(filter_callbacks_.connection_, close(Network::ConnectionCloseType::NoFlush, _));
  EXPECT_CALL(*client_, cancel()).Times(0);
  request_callbacks_->complete(Filters::Common::RateLimit::LimitStatus::OverLimit, nullptr, nullptr,
                               nullptr, "", std::move(dynamic_metadata));

  EXPECT_EQ(Network::FilterStatus::Continue, filter_->onData(data, false));

  EXPECT_EQ(1U, stats_store_.counter("ratelimit.name.total").value());
  EXPECT_EQ(1U, stats_store_.counter("ratelimit.name.over_limit").value());
  EXPECT_EQ(1U, stats_store_.counter("ratelimit.name.cx_closed").value());
}

TEST_F(RateLimitFilterTest, OverLimitNotEnforcing) {
  InSequence s;
  setUpTest(filter_config_);

  EXPECT_CALL(*client_, limit(_, "foo", _, _, _, 0))
      .WillOnce(
          WithArgs<0>(Invoke([&](Filters::Common::RateLimit::RequestCallbacks& callbacks) -> void {
            request_callbacks_ = &callbacks;
          })));

  EXPECT_EQ(Network::FilterStatus::StopIteration, filter_->onNewConnection());
  Buffer::OwnedImpl data("hello");
  EXPECT_EQ(Network::FilterStatus::StopIteration, filter_->onData(data, false));

  EXPECT_CALL(runtime_.snapshot_, featureEnabled("ratelimit.tcp_filter_enforcing", 100))
      .WillOnce(Return(false));
  EXPECT_CALL(filter_callbacks_.connection_, close(_, _)).Times(0);
  EXPECT_CALL(*client_, cancel()).Times(0);
  EXPECT_CALL(filter_callbacks_, continueReading());
  request_callbacks_->complete(Filters::Common::RateLimit::LimitStatus::OverLimit, nullptr, nullptr,
                               nullptr, "", nullptr);

  EXPECT_EQ(Network::FilterStatus::Continue, filter_->onData(data, false));

  EXPECT_EQ(1U, stats_store_.counter("ratelimit.name.total").value());
  EXPECT_EQ(1U, stats_store_.counter("ratelimit.name.over_limit").value());
  EXPECT_EQ(0U, stats_store_.counter("ratelimit.name.cx_closed").value());
}

TEST_F(RateLimitFilterTest, Error) {
  InSequence s;
  setUpTest(filter_config_);

  EXPECT_CALL(*client_, limit(_, "foo", _, _, _, 0))
      .WillOnce(
          WithArgs<0>(Invoke([&](Filters::Common::RateLimit::RequestCallbacks& callbacks) -> void {
            request_callbacks_ = &callbacks;
          })));

  EXPECT_EQ(Network::FilterStatus::StopIteration, filter_->onNewConnection());
  Buffer::OwnedImpl data("hello");
  EXPECT_EQ(Network::FilterStatus::StopIteration, filter_->onData(data, false));

  EXPECT_CALL(filter_callbacks_, continueReading());
  request_callbacks_->complete(Filters::Common::RateLimit::LimitStatus::Error, nullptr, nullptr,
                               nullptr, "", nullptr);

  EXPECT_EQ(Network::FilterStatus::Continue, filter_->onData(data, false));

  EXPECT_CALL(*client_, cancel()).Times(0);
  filter_callbacks_.connection_.raiseEvent(Network::ConnectionEvent::RemoteClose);

  EXPECT_EQ(1U, stats_store_.counter("ratelimit.name.total").value());
  EXPECT_EQ(1U, stats_store_.counter("ratelimit.name.error").value());
  EXPECT_EQ(1U, stats_store_.counter("ratelimit.name.failure_mode_allowed").value());
}

TEST_F(RateLimitFilterTest, Disconnect) {
  InSequence s;
  setUpTest(filter_config_);

  EXPECT_CALL(*client_, limit(_, "foo", _, _, _, 0))
      .WillOnce(
          WithArgs<0>(Invoke([&](Filters::Common::RateLimit::RequestCallbacks& callbacks) -> void {
            request_callbacks_ = &callbacks;
          })));

  EXPECT_EQ(Network::FilterStatus::StopIteration, filter_->onNewConnection());
  Buffer::OwnedImpl data("hello");
  EXPECT_EQ(Network::FilterStatus::StopIteration, filter_->onData(data, false));

  EXPECT_CALL(*client_, cancel());
  filter_callbacks_.connection_.raiseEvent(Network::ConnectionEvent::RemoteClose);

  EXPECT_EQ(1U, stats_store_.counter("ratelimit.name.total").value());
}

TEST_F(RateLimitFilterTest, ImmediateOK) {
  InSequence s;
  setUpTest(filter_config_);

  EXPECT_CALL(filter_callbacks_, continueReading()).Times(0);
  EXPECT_CALL(*client_, limit(_, "foo", _, _, _, 0))
      .WillOnce(
          WithArgs<0>(Invoke([&](Filters::Common::RateLimit::RequestCallbacks& callbacks) -> void {
            callbacks.complete(Filters::Common::RateLimit::LimitStatus::OK, nullptr, nullptr,
                               nullptr, "", nullptr);
          })));

  EXPECT_EQ(Network::FilterStatus::Continue, filter_->onNewConnection());
  Buffer::OwnedImpl data("hello");
  EXPECT_EQ(Network::FilterStatus::Continue, filter_->onData(data, false));
  EXPECT_EQ(Network::FilterStatus::Continue, filter_->onData(data, false));

  EXPECT_CALL(*client_, cancel()).Times(0);
  filter_callbacks_.connection_.raiseEvent(Network::ConnectionEvent::RemoteClose);

  EXPECT_EQ(1U, stats_store_.counter("ratelimit.name.total").value());
  EXPECT_EQ(1U, stats_store_.counter("ratelimit.name.ok").value());
}

TEST_F(RateLimitFilterTest, ImmediateError) {
  InSequence s;
  setUpTest(filter_config_);

  EXPECT_CALL(filter_callbacks_, continueReading()).Times(0);
  EXPECT_CALL(*client_, limit(_, "foo", _, _, _, 0))
      .WillOnce(
          WithArgs<0>(Invoke([&](Filters::Common::RateLimit::RequestCallbacks& callbacks) -> void {
            callbacks.complete(Filters::Common::RateLimit::LimitStatus::Error, nullptr, nullptr,
                               nullptr, "", nullptr);
          })));

  EXPECT_EQ(Network::FilterStatus::Continue, filter_->onNewConnection());
  Buffer::OwnedImpl data("hello");
  EXPECT_EQ(Network::FilterStatus::Continue, filter_->onData(data, false));
  EXPECT_EQ(Network::FilterStatus::Continue, filter_->onData(data, false));

  EXPECT_CALL(*client_, cancel()).Times(0);
  filter_callbacks_.connection_.raiseEvent(Network::ConnectionEvent::RemoteClose);

  EXPECT_EQ(1U, stats_store_.counter("ratelimit.name.total").value());
  EXPECT_EQ(1U, stats_store_.counter("ratelimit.name.error").value());
  EXPECT_EQ(1U, stats_store_.counter("ratelimit.name.failure_mode_allowed").value());
}

TEST_F(RateLimitFilterTest, RuntimeDisable) {
  InSequence s;
  setUpTest(filter_config_);

  EXPECT_CALL(runtime_.snapshot_, featureEnabled("ratelimit.tcp_filter_enabled", 100))
      .WillOnce(Return(false));
  EXPECT_CALL(*client_, limit(_, _, _, _, _, 0)).Times(0);

  EXPECT_EQ(Network::FilterStatus::Continue, filter_->onNewConnection());
  Buffer::OwnedImpl data("hello");
  EXPECT_EQ(Network::FilterStatus::Continue, filter_->onData(data, false));
}

TEST_F(RateLimitFilterTest, ErrorResponseWithFailureModeAllowOff) {
  InSequence s;
  setUpTest(fail_close_config_);

  EXPECT_CALL(*client_, limit(_, "foo", _, _, _, 0))
      .WillOnce(
          WithArgs<0>(Invoke([&](Filters::Common::RateLimit::RequestCallbacks& callbacks) -> void {
            request_callbacks_ = &callbacks;
          })));

  EXPECT_EQ(Network::FilterStatus::StopIteration, filter_->onNewConnection());
  Buffer::OwnedImpl data("hello");
  EXPECT_EQ(Network::FilterStatus::StopIteration, filter_->onData(data, false));
  request_callbacks_->complete(Filters::Common::RateLimit::LimitStatus::Error, nullptr, nullptr,
                               nullptr, "", nullptr);

  EXPECT_EQ(Network::FilterStatus::Continue, filter_->onData(data, false));

  EXPECT_CALL(*client_, cancel()).Times(0);
  filter_callbacks_.connection_.raiseEvent(Network::ConnectionEvent::RemoteClose);

  EXPECT_EQ(1U, stats_store_.counter("ratelimit.name.total").value());
  EXPECT_EQ(1U, stats_store_.counter("ratelimit.name.error").value());
  EXPECT_EQ(0U, stats_store_.counter("ratelimit.name.failure_mode_allowed").value());
}

class NetworkFilterManagerRateLimitTest : public testing::Test {
public:
  void SetUp() override {
    EXPECT_CALL(connection_, getReadBuffer).WillRepeatedly(Invoke([this]() {
      return Network::StreamBuffer{read_buffer_, read_end_stream_};
    }));
    EXPECT_CALL(connection_, getWriteBuffer).WillRepeatedly(Invoke([this]() {
      return Network::StreamBuffer{write_buffer_, write_end_stream_};
    }));
  }

  NiceMock<Network::MockFilterManagerConnection> connection_;
  NiceMock<Network::MockListenSocket> socket_;

  Buffer::OwnedImpl read_buffer_;
  Buffer::OwnedImpl write_buffer_;
  bool read_end_stream_{};
  bool write_end_stream_{};
};

// This is a very important flow so make sure it works correctly in aggregate.
TEST_F(NetworkFilterManagerRateLimitTest, RateLimitAndTcpProxy) {
  InSequence s;
  NiceMock<Server::Configuration::MockFactoryContext> factory_context;
  NiceMock<Network::MockClientConnection> upstream_connection;
  NiceMock<Tcp::ConnectionPool::MockInstance> conn_pool;
  Network::FilterManagerImpl manager(connection_, socket_);

  std::string rl_yaml = R"EOF(
domain: foo
descriptors:
- entries:
  - key: hello
    value: world
stat_prefix: name
    )EOF";

  ON_CALL(factory_context.runtime_loader_.snapshot_,
          featureEnabled("ratelimit.tcp_filter_enabled", 100))
      .WillByDefault(Return(true));
  ON_CALL(factory_context.runtime_loader_.snapshot_,
          featureEnabled("ratelimit.tcp_filter_enforcing", 100))
      .WillByDefault(Return(true));

  envoy::extensions::filters::network::ratelimit::v3::RateLimit proto_config{};
  TestUtility::loadFromYaml(rl_yaml, proto_config);

  Extensions::NetworkFilters::RateLimitFilter::ConfigSharedPtr rl_config(
      new Extensions::NetworkFilters::RateLimitFilter::Config(proto_config, factory_context.scope_,
                                                              factory_context.runtime_loader_));
  Extensions::Filters::Common::RateLimit::MockClient* rl_client =
      new Extensions::Filters::Common::RateLimit::MockClient();
  manager.addReadFilter(std::make_shared<Extensions::NetworkFilters::RateLimitFilter::Filter>(
      rl_config, Extensions::Filters::Common::RateLimit::ClientPtr{rl_client}));

  factory_context.cluster_manager_.initializeThreadLocalClusters({"fake_cluster"});
  envoy::extensions::filters::network::tcp_proxy::v3::TcpProxy tcp_proxy;
  tcp_proxy.set_stat_prefix("name");
  tcp_proxy.set_cluster("fake_cluster");
  TcpProxy::ConfigSharedPtr tcp_proxy_config(new TcpProxy::Config(tcp_proxy, factory_context));
  manager.addReadFilter(
      std::make_shared<TcpProxy::Filter>(tcp_proxy_config, factory_context.cluster_manager_));

  Extensions::Filters::Common::RateLimit::RequestCallbacks* request_callbacks{};
  EXPECT_CALL(*rl_client, limit(_, "foo",
                                testing::ContainerEq(
                                    std::vector<RateLimit::Descriptor>{{{{"hello", "world"}}}}),
                                testing::A<Tracing::Span&>(), _, 0))
      .WillOnce(WithArgs<0>(
          Invoke([&](Extensions::Filters::Common::RateLimit::RequestCallbacks& callbacks) -> void {
            request_callbacks = &callbacks;
          })));

  EXPECT_EQ(manager.initializeReadFilters(), true);

  EXPECT_CALL(factory_context.cluster_manager_.thread_local_cluster_, tcpConnPool(_, _))
      .WillOnce(Return(Upstream::TcpPoolData([]() {}, &conn_pool)));

  request_callbacks->complete(Extensions::Filters::Common::RateLimit::LimitStatus::OK, nullptr,
                              nullptr, nullptr, "", nullptr);
  conn_pool.poolReady(upstream_connection);

  Buffer::OwnedImpl buffer("hello");
  EXPECT_CALL(upstream_connection, write(BufferEqual(&buffer), _));
  read_buffer_.add("hello");
  manager.onRead();

  connection_.raiseEvent(Network::ConnectionEvent::RemoteClose);
}

} // namespace RateLimitFilter
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
