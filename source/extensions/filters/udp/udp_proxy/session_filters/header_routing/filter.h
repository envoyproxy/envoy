#pragma once

#include "envoy/extensions/filters/udp/udp_proxy/session/header_routing/v3/header_routing.pb.h"
#include "envoy/network/filter.h"
#include "envoy/server/filter_config.h"
#include "envoy/stats/stats_macros.h"

#include "source/common/common/logger.h"
#include "source/common/header_routing/header_parser.h"

namespace Envoy {
namespace Extensions {
namespace UdpFilters {
namespace UdpProxy {
namespace SessionFilters {
namespace HeaderRouting {

// 便捷别名：共享无状态 Parser 组件。
using Parser = ::Envoy::HeaderRouting::HeaderParser;
using ParserConfig = ::Envoy::HeaderRouting::HeaderRoutingConfig;
using ParseResult = ::Envoy::HeaderRouting::ParseResult;
using ParsedTarget = ::Envoy::HeaderRouting::ParsedTarget;

/**
 * All header_routing UDP session filter stats. @see stats_macros.h
 */
#define ALL_HEADER_ROUTING_UDP_STATS(COUNTER) COUNTER(dropped)

/**
 * Struct definition for all header_routing UDP session filter stats. @see stats_macros.h
 */
struct HeaderRoutingUdpStats {
  ALL_HEADER_ROUTING_UDP_STATS(GENERATE_COUNTER_STRUCT)
};

// 跨 session 共享的过滤器配置：协议参数（magic/version）+ 统计。
// 由工厂在配置期构造一次，运行时每个 session 构造 filter 时传入。
class HeaderRoutingUdpFilterConfig {
public:
  HeaderRoutingUdpFilterConfig(
      const envoy::extensions::filters::udp::udp_proxy::session::header_routing::v3::HeaderRouting&
          proto_config,
      Server::Configuration::FactoryContext& context);

  const ParserConfig& parserConfig() const { return parser_config_; }
  HeaderRoutingUdpStats& stats() { return stats_; }

private:
  static HeaderRoutingUdpStats generateStats(Stats::Scope& scope) {
    return {ALL_HEADER_ROUTING_UDP_STATS(POOL_COUNTER(scope))};
  }

  ParserConfig parser_config_;
  Stats::ScopeSharedPtr scope_;
  HeaderRoutingUdpStats stats_;
};

using HeaderRoutingUdpFilterConfigSharedPtr = std::shared_ptr<HeaderRoutingUdpFilterConfig>;

using ReadFilter = Network::UdpSessionReadFilter;
using ReadFilterStatus = Network::UdpSessionReadFilterStatus;
using ReadFilterCallbacks = Network::UdpSessionReadFilterCallbacks;

// UDP 适配器（UdpSessionReadFilter）：三步动态路由。
// ① onNewSession 阻断（阻止 udp_proxy 在 filter 链走完前选上游）
// ② onData 解析头部 → 剥离头部 → 写 dynamic_host/port filter state
// ③ continueFilterChain 续链（触发 DFP.onNewSession 读状态并选上游）
class HeaderRoutingUdpFilter : public ReadFilter, Logger::Loggable<Logger::Id::filter> {
public:
  HeaderRoutingUdpFilter(HeaderRoutingUdpFilterConfigSharedPtr config)
      : config_(std::move(config)) {}

  // Network::ReadFilter
  ReadFilterStatus onNewSession() override;
  ReadFilterStatus onData(Network::UdpRecvData& data) override;

  void initializeReadFilterCallbacks(ReadFilterCallbacks& callbacks) override {
    read_callbacks_ = &callbacks;
  }

private:
  // 写入 DFP 读取的目标地址 filter state（类型与 DFP 读取类型严格一致）。
  void setTargetFilterState(const ParsedTarget& target);
  // 丢弃畸形数据报：整包排空 + 统计。
  void dropDatagram(Network::UdpRecvData& data);

  const HeaderRoutingUdpFilterConfigSharedPtr config_;
  ReadFilterCallbacks* read_callbacks_{};
  // 会话头部是否已成功解析（配合"确认前带头、确认后停"契约，每个会话一个 filter 实例）。
  bool header_handled_{false};
};

} // namespace HeaderRouting
} // namespace SessionFilters
} // namespace UdpProxy
} // namespace UdpFilters
} // namespace Extensions
} // namespace Envoy
