#include "envoy/extensions/filters/udp/udp_proxy/session/header_routing/v3/header_routing.pb.h"
#include "envoy/network/listener.h"
#include "envoy/router/string_accessor.h"
#include "envoy/stream_info/uint32_accessor.h"

#include "source/common/buffer/buffer_impl.h"
#include "source/extensions/filters/udp/udp_proxy/session_filters/header_routing/filter.h"

#include "test/extensions/filters/udp/udp_proxy/mocks.h"
#include "test/mocks/server/factory_context.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::_;
using testing::NiceMock;
using testing::Return;
using testing::ReturnRef;

namespace Envoy {
namespace Extensions {
namespace UdpFilters {
namespace UdpProxy {
namespace SessionFilters {
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

class HeaderRoutingUdpFilterTest : public testing::Test {
public:
  void setup(uint32_t magic = 0x55, uint32_t version = 1, bool forward_header = true) {
    envoy::extensions::filters::udp::udp_proxy::session::header_routing::v3::HeaderRouting
        proto_config;
    proto_config.set_magic(magic);
    proto_config.set_version(version);
    // 默认参数与产品默认（true=透传头部）对齐；剥离语义的用例显式传 false。
    proto_config.set_forward_header(forward_header);
    config_ = std::make_shared<HeaderRoutingUdpFilterConfig>(proto_config, context_);
    filter_ = std::make_unique<HeaderRoutingUdpFilter>(config_);
    filter_->initializeReadFilterCallbacks(callbacks_);
    ON_CALL(callbacks_, streamInfo()).WillByDefault(ReturnRef(stream_info_));
    // 模拟"续链成功"：会话保留，filter 才能返回 Continue。
    ON_CALL(callbacks_, continueFilterChain()).WillByDefault(Return(true));
  }

  // 构造数据报：header + payload。
  Network::UdpRecvData makeDatagram(const std::string& header, const std::string& payload) {
    Network::UdpRecvData data;
    data.buffer_ = std::make_unique<Buffer::OwnedImpl>(header + payload);
    return data;
  }

  NiceMock<Server::Configuration::MockFactoryContext> context_;
  HeaderRoutingUdpFilterConfigSharedPtr config_;
  std::unique_ptr<HeaderRoutingUdpFilter> filter_;
  NiceMock<MockReadFilterCallbacks> callbacks_;
  NiceMock<StreamInfo::MockStreamInfo> stream_info_;
};

// ① 阻断 + 首包解析：剥离头部（forward_header=false）、写 filter state、续链恰好一次。
TEST_F(HeaderRoutingUdpFilterTest, ParsesAndStripsHeaderOnFirstDatagram) {
  setup(0x55, 1, false);
  EXPECT_EQ(ReadFilterStatus::StopIteration, filter_->onNewSession());

  Network::UdpRecvData data = makeDatagram(makeHeader(0x55, 1, 0x0A000003, 8600), "game");

  EXPECT_CALL(callbacks_, continueFilterChain()).Times(1);
  EXPECT_EQ(ReadFilterStatus::Continue, filter_->onData(data));

  // 头部已被剥离，仅剩游戏数据。
  EXPECT_EQ(4U, data.buffer_->length());
  EXPECT_EQ("game", data.buffer_->toString());
  // filter state 已写入 DFP 读取的键（类型一致）。
  const auto* host = stream_info_.filterState()->getDataReadOnly<::Envoy::Router::StringAccessor>(
      "envoy.upstream.dynamic_host");
  ASSERT_NE(nullptr, host);
  EXPECT_EQ("10.0.0.3", host->asString());
  const auto* port = stream_info_.filterState()->getDataReadOnly<StreamInfo::UInt32Accessor>(
      "envoy.upstream.dynamic_port");
  ASSERT_NE(nullptr, port);
  EXPECT_EQ(8600, port->value());
}

// 已确认（header_handled_ == true）：后续数据报透传，不再解析/剥离/续链。
TEST_F(HeaderRoutingUdpFilterTest, PassesThroughAfterHeaderHandled) {
  setup(0x55, 1, false);
  EXPECT_EQ(ReadFilterStatus::StopIteration, filter_->onNewSession());

  Network::UdpRecvData first = makeDatagram(makeHeader(0x55, 1, 0x0A000003, 8600), "game");
  EXPECT_CALL(callbacks_, continueFilterChain()).Times(1);
  EXPECT_EQ(ReadFilterStatus::Continue, filter_->onData(first));

  // 后续无头数据报：原样透传（不再有 continueFilterChain、不再剥离）。
  Network::UdpRecvData second = makeDatagram("", "no-header");
  EXPECT_EQ(ReadFilterStatus::Continue, filter_->onData(second));
  EXPECT_EQ(9U, second.buffer_->length());
  EXPECT_EQ("no-header", second.buffer_->toString());
}

// 畸形包（Magic 错）：丢弃整包 + 统计，不续链，阻断后续 filter 处理。
TEST_F(HeaderRoutingUdpFilterTest, DropsBadMagicDatagram) {
  setup();
  EXPECT_EQ(ReadFilterStatus::StopIteration, filter_->onNewSession());

  Network::UdpRecvData data = makeDatagram(makeHeader(0x54, 1, 0x0A000003, 8600), "game");
  EXPECT_CALL(callbacks_, continueFilterChain()).Times(0);
  EXPECT_EQ(ReadFilterStatus::StopIteration, filter_->onData(data));
  EXPECT_EQ(0U, data.buffer_->length()); // 整包被丢弃
  EXPECT_EQ(1U, config_->stats().dropped_.value());
}

// 畸形包（Version 错）：丢弃整包 + 统计。
TEST_F(HeaderRoutingUdpFilterTest, DropsBadVersionDatagram) {
  setup();
  EXPECT_EQ(ReadFilterStatus::StopIteration, filter_->onNewSession());

  Network::UdpRecvData data = makeDatagram(makeHeader(0x55, 2, 0x0A000003, 8600), "game");
  EXPECT_EQ(ReadFilterStatus::StopIteration, filter_->onData(data));
  EXPECT_EQ(0U, data.buffer_->length());
  EXPECT_EQ(1U, config_->stats().dropped_.value());
}

// 短包（< 8 字节）：丢弃整包 + 统计。
TEST_F(HeaderRoutingUdpFilterTest, DropsShortDatagram) {
  setup();
  EXPECT_EQ(ReadFilterStatus::StopIteration, filter_->onNewSession());

  Network::UdpRecvData data = makeDatagram("", "abc"); // 3 字节
  EXPECT_EQ(ReadFilterStatus::StopIteration, filter_->onData(data));
  EXPECT_EQ(0U, data.buffer_->length());
  EXPECT_EQ(1U, config_->stats().dropped_.value());
}

// 自定义 magic/version 配置生效：默认配置下该头为畸形，自定义配置下可解析。
TEST_F(HeaderRoutingUdpFilterTest, UsesCustomMagicAndVersion) {
  setup(0xAA, 3, false);
  EXPECT_EQ(ReadFilterStatus::StopIteration, filter_->onNewSession());

  Network::UdpRecvData data = makeDatagram(makeHeader(0xAA, 3, 0x0A000004, 9000), "game");
  EXPECT_CALL(callbacks_, continueFilterChain()).Times(1);
  EXPECT_EQ(ReadFilterStatus::Continue, filter_->onData(data));
  EXPECT_EQ(4U, data.buffer_->length());
  const auto* host = stream_info_.filterState()->getDataReadOnly<::Envoy::Router::StringAccessor>(
      "envoy.upstream.dynamic_host");
  ASSERT_NE(nullptr, host);
  EXPECT_EQ("10.0.0.4", host->asString());
}

// forward_header=true：解析选路后保留 8B 头，头部连同游戏数据原样转发给上游。
TEST_F(HeaderRoutingUdpFilterTest, ForwardsHeaderWhenConfigured) {
  setup(); // 默认 true（透传头部）
  EXPECT_EQ(ReadFilterStatus::StopIteration, filter_->onNewSession());

  const std::string header = makeHeader(0x55, 1, 0x0A000003, 8600);
  Network::UdpRecvData data = makeDatagram(header, "game");

  EXPECT_CALL(callbacks_, continueFilterChain()).Times(1);
  EXPECT_EQ(ReadFilterStatus::Continue, filter_->onData(data));

  // 头部保留：8B 头 + 4B 游戏数据原样转发（不剥离）。
  EXPECT_EQ(12U, data.buffer_->length());
  EXPECT_EQ(header + "game", data.buffer_->toString());
  // filter state 仍写入选路信息。
  const auto* host = stream_info_.filterState()->getDataReadOnly<::Envoy::Router::StringAccessor>(
      "envoy.upstream.dynamic_host");
  ASSERT_NE(nullptr, host);
  EXPECT_EQ("10.0.0.3", host->asString());
  const auto* port = stream_info_.filterState()->getDataReadOnly<StreamInfo::UInt32Accessor>(
      "envoy.upstream.dynamic_port");
  ASSERT_NE(nullptr, port);
  EXPECT_EQ(8600, port->value());
}

// 未配置 forward_header 字段：走产品默认 true（透传头部）分支。
TEST_F(HeaderRoutingUdpFilterTest, DefaultsToForwardingHeader) {
  envoy::extensions::filters::udp::udp_proxy::session::header_routing::v3::HeaderRouting
      proto_config;
  proto_config.set_magic(0x55);
  proto_config.set_version(1);
  // 故意不设置 forward_header：验证 has_forward_header()==false 时默认 true。
  config_ = std::make_shared<HeaderRoutingUdpFilterConfig>(proto_config, context_);
  EXPECT_TRUE(config_->parserConfig().forward_header);
}

} // namespace
} // namespace HeaderRouting
} // namespace SessionFilters
} // namespace UdpProxy
} // namespace UdpFilters
} // namespace Extensions
} // namespace Envoy
