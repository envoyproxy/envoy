#include "source/extensions/filters/network/header_routing/filter.h"

#include "envoy/buffer/buffer.h"
#include "envoy/common/exception.h"
#include "envoy/network/connection.h"
#include "envoy/stream_info/filter_state.h"
#include "envoy/stream_info/uint32_accessor.h"

#include "source/common/common/assert.h"
#include "source/common/header_routing/header_parser.h"
#include "source/common/router/string_accessor_impl.h"
#include "source/common/stream_info/uint32_accessor_impl.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace HeaderRouting {

// 头部总长度（字节），与共享 Parser 的常量保持一致。
constexpr size_t HeaderLength = ::Envoy::HeaderRouting::HeaderLength;

HeaderRoutingTcpFilterConfig::HeaderRoutingTcpFilterConfig(
    const envoy::extensions::filters::network::header_routing::v3::HeaderRouting& proto_config,
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

Network::FilterStatus HeaderRoutingTcpFilter::onNewConnection() {
  // ① 阻断：阻止 sni_dynamic_forward_proxy 在头部就绪前选上游。
  return Network::FilterStatus::StopIteration;
}

Network::FilterStatus HeaderRoutingTcpFilter::onData(Buffer::Instance& data, bool end_stream) {
  // 头部已处理：纯游戏数据，全部透传。
  if (header_handled_) {
    return Network::FilterStatus::Continue;
  }

  // TCP 是字节流，头部可能跨 segment。
  // 注意：data 即连接读缓冲本身，跨 onData 调用天然累积，无需额外 pending_buffer_；
  // 返回 StopIteration 时未消费的字节保留在连接读缓冲中，等待后续 segment 补齐。
  if (data.length() < HeaderLength) {
    // 连接已半关闭但头部仍不完整：永远无法补齐，按畸形头关闭连接。
    if (end_stream) {
      config_->stats().invalid_.inc();
      read_callbacks_->connection().close(Network::ConnectionCloseType::NoFlush);
    }
    return Network::FilterStatus::StopIteration;
  }

  ParseResult result = Parser::parse(
      absl::string_view(static_cast<const char*>(data.linearize(HeaderLength)), HeaderLength),
      config_->parserConfig());

  switch (result.status) {
  case ParseResult::Status::Ok:
    // ② 解析成功：写 filter state、续链触发 sni_dynamic_forward_proxy 选上游。
    // forward_header=true（默认）保留 8B 头原样转发给上游；
    // forward_header=false 时剥离协议头，仅转发游戏数据。
    if (!config_->parserConfig().forward_header) {
      data.drain(HeaderLength);
    }
    setTargetFilterState(result.target.value());
    header_handled_ = true;
    // continueReading() 已递归接管后续 filter（sni_dynamic_forward_proxy → tcp_proxy）：
    //  - DNS 命中（InCache）时同步完成后续链路与首包转发；
    //  - DNS 异步（Loading）时由 onLoadDnsCacheComplete 回调续链。
    // 故此处返回 StopIteration，避免外层循环重复推进到 tcp_proxy，
    // 导致其在 sni 尚未完成 DNS 解析、DFP cluster 无 host 时被提前初始化而失败。
    read_callbacks_->continueReading();
    return Network::FilterStatus::StopIteration;
  case ParseResult::Status::NeedMoreData:
    // 理论上到达此处时长度已 >= HeaderLength，此处防御性兜底：等待更多数据。
    return Network::FilterStatus::StopIteration;
  case ParseResult::Status::BadMagic:
  case ParseResult::Status::BadVersion:
    // 畸形头，关闭连接（客户端需自行重试）。
    config_->stats().invalid_.inc();
    read_callbacks_->connection().close(Network::ConnectionCloseType::NoFlush);
    return Network::FilterStatus::StopIteration;
  }
  PANIC_DUE_TO_CORRUPT_ENUM;
}

void HeaderRoutingTcpFilter::setTargetFilterState(const ParsedTarget& target) {
  // 类型与 sni_dynamic_forward_proxy 读取类型严格一致：Router::StringAccessor + StreamInfo::UInt32Accessor。
  // 注意：Envoy 1.39 的 setData 签名为 (name, data, LifeSpan, StreamSharing)，无 StateType；
  // 只读语义由读取方 getDataReadOnly 保证。
  read_callbacks_->connection().streamInfo().filterState()->setData(
      "envoy.upstream.dynamic_host", std::make_shared<Router::StringAccessorImpl>(target.ip),
      StreamInfo::FilterState::LifeSpan::Connection);
  read_callbacks_->connection().streamInfo().filterState()->setData(
      "envoy.upstream.dynamic_port",
      std::make_shared<StreamInfo::UInt32AccessorImpl>(target.port),
      StreamInfo::FilterState::LifeSpan::Connection);
}

} // namespace HeaderRouting
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
