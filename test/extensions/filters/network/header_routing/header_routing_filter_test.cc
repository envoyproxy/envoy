#include "envoy/extensions/filters/network/header_routing/v3/header_routing.pb.h"
#include "envoy/router/string_accessor.h"
#include "envoy/stream_info/uint32_accessor.h"

#include "source/common/buffer/buffer_impl.h"
#include "source/extensions/filters/network/header_routing/filter.h"

#include "test/mocks/network/mocks.h"
#include "test/mocks/server/factory_context.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::_;
using testing::NiceMock;

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace HeaderRouting {
namespace {

// 构造 8 字节头部：Magic/Version 单字节 + RoomIP 4 字节大端 + RoomPort 2 字节大端。
std::string makeHeader(uint8_t magic, uint8_t version, uint32_t ip, uint16_t port) {
  std::string header(::Envoy::HeaderRouting::HeaderLength, '\0');
  header[::Envoy::HeaderRouting::MagicOffset] = static_cast<char>(magic);
  header[::Envoy::HeaderRouting::VersionOffset] = static_cast<char>(version);
  header[::Envoy::HeaderRouting::IpOffset] = static_cast<char>((ip >> 24) & 0xFF);
  header[::Envoy::HeaderRouting::IpOffset + 1] = static_cast<char>((ip >> 16) & 0xFF);
  header[::Envoy::HeaderRouting::IpOffset + 2] = static_cast<char>((ip >> 8) & 0xFF);
  header[::Envoy::HeaderRouting::IpOffset + 3] = static_cast<char>(ip & 0xFF);
  header[::Envoy::HeaderRouting::PortOffset] = static_cast<char>((port >> 8) & 0xFF);
  header[::Envoy::HeaderRouting::PortOffset + 1] = static_cast<char>(port & 0xFF);
  return header;
}

class HeaderRoutingTcpFilterTest : public testing::Test {
public:
  void setup(uint32_t magic = 0x55, uint32_t version = 1, bool forward_header = true) {
    envoy::extensions::filters::network::header_routing::v3::HeaderRouting proto_config;
    proto_config.set_magic(magic);
    proto_config.set_version(version);
    // 默认参数与产品默认（true=透传头部）对齐；剥离语义的用例显式传 false。
    proto_config.set_forward_header(forward_header);
    config_ = std::make_shared<HeaderRoutingTcpFilterConfig>(proto_config, context_);
    filter_ = std::make_unique<HeaderRoutingTcpFilter>(config_);
    filter_->initializeReadFilterCallbacks(callbacks_);
  }

  NiceMock<Server::Configuration::MockFactoryContext> context_;
  HeaderRoutingTcpFilterConfigSharedPtr config_;
  std::unique_ptr<HeaderRoutingTcpFilter> filter_;
  NiceMock<Network::MockReadFilterCallbacks> callbacks_;
};

// ① 阻断 + 完整头一次到达：剥离头部（forward_header=false）、写 filter state、续链恰好一次。
TEST_F(HeaderRoutingTcpFilterTest, ParsesAndStripsHeaderOnFirstData) {
  setup(0x55, 1, false);
  EXPECT_EQ(Network::FilterStatus::StopIteration, filter_->onNewConnection());

  Buffer::OwnedImpl buffer(makeHeader(0x55, 1, 0x0A000003, 8600) + "game");
  EXPECT_CALL(callbacks_, continueReading()).Times(1);
  EXPECT_EQ(Network::FilterStatus::Continue, filter_->onData(buffer, false));

  // 头部被剥离，剩余游戏数据保留在连接读缓冲（供后续过滤器读取）。
  EXPECT_EQ(4U, buffer.length());
  EXPECT_EQ("game", buffer.toString());
  // filter state 已写入 sni_dynamic_forward_proxy 读取的键（类型一致）。
  const auto* host = callbacks_.connection_.stream_info_.filterState()
                         ->getDataReadOnly<::Envoy::Router::StringAccessor>("envoy.upstream.dynamic_host");
  ASSERT_NE(nullptr, host);
  EXPECT_EQ("10.0.0.3", host->asString());
  const auto* port = callbacks_.connection_.stream_info_.filterState()
                         ->getDataReadOnly<StreamInfo::UInt32Accessor>(
                             "envoy.upstream.dynamic_port");
  ASSERT_NE(nullptr, port);
  EXPECT_EQ(8600, port->value());
}

// 头部跨 segment 累积：同一连接读缓冲追加数据（模拟 TCP 字节流天然累积），
// 首段不足 8 字节时返回 StopIteration 且不清空缓冲，补齐后解析并续链。
TEST_F(HeaderRoutingTcpFilterTest, AccumulatesPartialHeaderAcrossSegments) {
  setup(0x55, 1, false);
  EXPECT_EQ(Network::FilterStatus::StopIteration, filter_->onNewConnection());

  Buffer::OwnedImpl buffer(makeHeader(0x55, 1, 0x0A000003, 8600).substr(0, 5)); // 前 5 字节
  EXPECT_EQ(Network::FilterStatus::StopIteration, filter_->onData(buffer, false));
  EXPECT_EQ(5U, buffer.length()); // 未消费数据保留在缓冲中

  // 第二段补齐剩余头部 + 游戏数据：缓冲累积为 8B 头 + 游戏数据。
  buffer.add(makeHeader(0x55, 1, 0x0A000003, 8600).substr(5) + "game");
  EXPECT_CALL(callbacks_, continueReading()).Times(1);
  EXPECT_EQ(Network::FilterStatus::Continue, filter_->onData(buffer, false));
  EXPECT_EQ(4U, buffer.length());
  EXPECT_EQ("game", buffer.toString());
}

// 头部已处理：后续数据透传，不再解析/剥离/续链。
TEST_F(HeaderRoutingTcpFilterTest, PassesThroughAfterHeaderHandled) {
  setup(0x55, 1, false);
  EXPECT_EQ(Network::FilterStatus::StopIteration, filter_->onNewConnection());

  Buffer::OwnedImpl first(makeHeader(0x55, 1, 0x0A000003, 8600) + "game");
  EXPECT_CALL(callbacks_, continueReading()).Times(1);
  EXPECT_EQ(Network::FilterStatus::Continue, filter_->onData(first, false));

  Buffer::OwnedImpl second("no-header-data");
  EXPECT_EQ(Network::FilterStatus::Continue, filter_->onData(second, false));
  EXPECT_EQ(14U, second.length()); // 原样透传
  EXPECT_EQ("no-header-data", second.toString());
}

// 畸形头（Magic 错）：关闭连接 + 统计 + StopIteration。
TEST_F(HeaderRoutingTcpFilterTest, ClosesConnectionOnBadMagic) {
  setup();
  EXPECT_EQ(Network::FilterStatus::StopIteration, filter_->onNewConnection());

  Buffer::OwnedImpl buffer(makeHeader(0x54, 1, 0x0A000003, 8600) + "game");
  EXPECT_CALL(callbacks_.connection_, close(Network::ConnectionCloseType::NoFlush)).Times(1);
  EXPECT_EQ(Network::FilterStatus::StopIteration, filter_->onData(buffer, false));
  EXPECT_EQ(1U, config_->stats().invalid_.value());
}

// 畸形头（Version 错）：关闭连接 + 统计。
TEST_F(HeaderRoutingTcpFilterTest, ClosesConnectionOnBadVersion) {
  setup();
  EXPECT_EQ(Network::FilterStatus::StopIteration, filter_->onNewConnection());

  Buffer::OwnedImpl buffer(makeHeader(0x55, 2, 0x0A000003, 8600) + "game");
  EXPECT_CALL(callbacks_.connection_, close(Network::ConnectionCloseType::NoFlush)).Times(1);
  EXPECT_EQ(Network::FilterStatus::StopIteration, filter_->onData(buffer, false));
  EXPECT_EQ(1U, config_->stats().invalid_.value());
}

// 连接半关闭但头部仍不完整（end_stream=true）：无法补齐，按畸形头关闭连接。
TEST_F(HeaderRoutingTcpFilterTest, ClosesConnectionOnEndStreamWithPartialHeader) {
  setup();
  EXPECT_EQ(Network::FilterStatus::StopIteration, filter_->onNewConnection());

  Buffer::OwnedImpl buffer("abc"); // 3 字节，头部不完整
  EXPECT_CALL(callbacks_.connection_, close(Network::ConnectionCloseType::NoFlush)).Times(1);
  EXPECT_EQ(Network::FilterStatus::StopIteration, filter_->onData(buffer, true));
  EXPECT_EQ(1U, config_->stats().invalid_.value());
}

// 自定义 magic/version 配置生效：默认配置下该头为畸形，自定义配置下可解析。
TEST_F(HeaderRoutingTcpFilterTest, UsesCustomMagicAndVersion) {
  setup(0xAA, 3, false);
  EXPECT_EQ(Network::FilterStatus::StopIteration, filter_->onNewConnection());

  Buffer::OwnedImpl buffer(makeHeader(0xAA, 3, 0x0A000004, 9000) + "game");
  EXPECT_CALL(callbacks_, continueReading()).Times(1);
  EXPECT_EQ(Network::FilterStatus::Continue, filter_->onData(buffer, false));
  const auto* host = callbacks_.connection_.stream_info_.filterState()
                         ->getDataReadOnly<::Envoy::Router::StringAccessor>("envoy.upstream.dynamic_host");
  ASSERT_NE(nullptr, host);
  EXPECT_EQ("10.0.0.4", host->asString());
}

// forward_header=true：解析选路后保留 8B 头，头部连同游戏数据原样转发给上游。
TEST_F(HeaderRoutingTcpFilterTest, ForwardsHeaderWhenConfigured) {
  setup(); // 默认 true（透传头部）
  EXPECT_EQ(Network::FilterStatus::StopIteration, filter_->onNewConnection());

  const std::string header = makeHeader(0x55, 1, 0x0A000003, 8600);
  Buffer::OwnedImpl buffer(header + "game");
  EXPECT_CALL(callbacks_, continueReading()).Times(1);
  EXPECT_EQ(Network::FilterStatus::Continue, filter_->onData(buffer, false));

  // 头部保留：8B 头 + 4B 游戏数据原样留在连接读缓冲（不剥离）。
  EXPECT_EQ(12U, buffer.length());
  EXPECT_EQ(header + "game", buffer.toString());
  // filter state 仍写入选路信息。
  const auto* host = callbacks_.connection_.stream_info_.filterState()
                         ->getDataReadOnly<::Envoy::Router::StringAccessor>("envoy.upstream.dynamic_host");
  ASSERT_NE(nullptr, host);
  EXPECT_EQ("10.0.0.3", host->asString());
  const auto* port = callbacks_.connection_.stream_info_.filterState()
                         ->getDataReadOnly<StreamInfo::UInt32Accessor>(
                             "envoy.upstream.dynamic_port");
  ASSERT_NE(nullptr, port);
  EXPECT_EQ(8600, port->value());
}

// 未配置 forward_header 字段：走产品默认 true（透传头部）分支。
TEST_F(HeaderRoutingTcpFilterTest, DefaultsToForwardingHeader) {
  envoy::extensions::filters::network::header_routing::v3::HeaderRouting proto_config;
  proto_config.set_magic(0x55);
  proto_config.set_version(1);
  // 故意不设置 forward_header：验证 has_forward_header()==false 时默认 true。
  config_ = std::make_shared<HeaderRoutingTcpFilterConfig>(proto_config, context_);
  EXPECT_TRUE(config_->parserConfig().forward_header);
}

} // namespace
} // namespace HeaderRouting
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
