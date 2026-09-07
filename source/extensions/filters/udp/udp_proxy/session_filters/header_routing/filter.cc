#include "source/extensions/filters/udp/udp_proxy/session_filters/header_routing/filter.h"

#include <algorithm>

#include "absl/strings/string_view.h"

#include "envoy/buffer/buffer.h"
#include "envoy/common/exception.h"
#include "envoy/network/listener.h"
#include "envoy/stream_info/filter_state.h"
#include "envoy/stream_info/uint32_accessor.h"

#include "source/common/common/assert.h"
#include "source/common/header_routing/header_parser.h"
#include "source/common/router/string_accessor_impl.h"
#include "source/common/stream_info/uint32_accessor_impl.h"

namespace Envoy {
namespace Extensions {
namespace UdpFilters {
namespace UdpProxy {
namespace SessionFilters {
namespace HeaderRouting {

// 头部总长度（字节），与共享 Parser 的常量保持一致。
constexpr size_t HeaderLength = ::Envoy::HeaderRouting::HeaderLength;

HeaderRoutingUdpFilterConfig::HeaderRoutingUdpFilterConfig(
    const envoy::extensions::filters::udp::udp_proxy::session::header_routing::v3::HeaderRouting&
        proto_config,
    Server::Configuration::FactoryContext& context)
    : scope_(context.scope().createScope("header_routing.")),
      stats_(generateStats(*scope_)) {
  // magic/version 为单字节字段，配置值超界视为配置错误。
  if (proto_config.has_magic() && proto_config.magic() > 255) {
    throw EnvoyException("header_routing: magic must be in the range [0, 255]");
  }
  if (proto_config.has_version() && proto_config.version() > 255) {
    throw EnvoyException("header_routing: version must be in the range [0, 255]");
  }
  parser_config_.magic =
      proto_config.has_magic() ? static_cast<uint8_t>(proto_config.magic()) : 0x55;
  parser_config_.version =
      proto_config.has_version() ? static_cast<uint8_t>(proto_config.version()) : 1;
  // forward_header 默认 true：解析选路后保留 8B 头原样转发给上游；
  // 显式配置 false 时剥离头部，仅转发游戏数据。
  parser_config_.forward_header =
      proto_config.has_forward_header() ? proto_config.forward_header() : true;
}

ReadFilterStatus HeaderRoutingUdpFilter::onNewSession() {
  // ① 阻断：阻止 udp_proxy 在 filter 链走完前选上游（setClusterInfo/createUpstream）。
  return ReadFilterStatus::StopIteration;
}

ReadFilterStatus HeaderRoutingUdpFilter::onData(Network::UdpRecvData& data) {
  // 已确认（header_handled_ == true）：客户端已不再带头，全部透传；
  // 且此时绝不能再调 continueFilterChain()（会重复 setClusterInfo/createUpstream）。
  if (header_handled_) {
    return ReadFilterStatus::Continue;
  }

  // 线性化前 8 字节用于解析；短包（< 8 字节）时视图长度收敛为实际长度，避免越界读取，
  // 不足头部长度的短包由 Parser 判 NeedMoreData。
  // linearize 内部有 RELEASE_ASSERT(size <= length)，短包(< HeaderLength)必须收敛 peek 长度，
  // 否则 linearize(HeaderLength) 的 size 会超出 buffer 长度而触发断言崩溃。
  const uint64_t peek_len = std::min<uint64_t>(HeaderLength, data.buffer_->length());
  ParseResult result = Parser::parse(
      absl::string_view(
          static_cast<const char*>(data.buffer_->linearize(static_cast<uint32_t>(peek_len))),
          peek_len),
      config_->parserConfig());

  switch (result.status) {
  case ParseResult::Status::Ok:
    // ② 解析成功：写 filter state、续链触发 DFP 选上游。
    // forward_header=true（默认）保留 8B 头原样转发给上游；
    // forward_header=false 时剥离协议头，仅转发游戏数据。
    if (!config_->parserConfig().forward_header) {
      data.buffer_->drain(HeaderLength);
    }
    setTargetFilterState(result.target.value());
    header_handled_ = true; // 只允许续链一次
    // 续链返回 false 表示会话已被移除（如 DFP 选上游失败），停止继续处理。
    if (!read_callbacks_->continueFilterChain()) {
      return ReadFilterStatus::StopIteration;
    }
    return ReadFilterStatus::Continue;
  case ParseResult::Status::NeedMoreData: // UDP 半包 = 畸形包
  case ParseResult::Status::BadMagic:
  case ParseResult::Status::BadVersion:
    // 丢包 + 统计；客户端应重发带头包（"确认前带头"自愈）。
    // 注意：返回 StopIteration 终止外层 onData 循环，避免被排空的空数据报
    // 继续流向 DFP/上游（否则 writeUpstream 会发出 0 字节数据报）。
    dropDatagram(data);
    return ReadFilterStatus::StopIteration;
  }
  PANIC_DUE_TO_CORRUPT_ENUM;
}

void HeaderRoutingUdpFilter::setTargetFilterState(const ParsedTarget& target) {
  // 类型与 UDP DFP 读取类型严格一致：Router::StringAccessor + StreamInfo::UInt32Accessor。
  // 注意：Envoy 1.39 的 setData 签名为 (name, data, LifeSpan, StreamSharing)，无 StateType；
  // 只读语义由读取方 getDataReadOnly 保证。
  read_callbacks_->streamInfo().filterState()->setData(
      "envoy.upstream.dynamic_host", std::make_shared<Router::StringAccessorImpl>(target.ip),
      StreamInfo::FilterState::LifeSpan::FilterChain);
  read_callbacks_->streamInfo().filterState()->setData(
      "envoy.upstream.dynamic_port",
      std::make_shared<StreamInfo::UInt32AccessorImpl>(target.port),
      StreamInfo::FilterState::LifeSpan::FilterChain);
}

void HeaderRoutingUdpFilter::dropDatagram(Network::UdpRecvData& data) {
  config_->stats().dropped_.inc();
  // 排空整个数据报，确保畸形包不会被透传转发。
  data.buffer_->drain(data.buffer_->length());
}

} // namespace HeaderRouting
} // namespace SessionFilters
} // namespace UdpProxy
} // namespace UdpFilters
} // namespace Extensions
} // namespace Envoy
