#pragma once

#include "envoy/extensions/filters/network/header_routing/v3/header_routing.pb.h"
#include "envoy/network/filter.h"
#include "envoy/server/filter_config.h"
#include "envoy/stats/stats_macros.h"

#include "source/common/common/logger.h"
#include "source/common/header_routing/header_parser.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace HeaderRouting {

// 便捷别名：共享无状态 Parser 组件。
using Parser = ::Envoy::HeaderRouting::HeaderParser;
using ParserConfig = ::Envoy::HeaderRouting::HeaderRoutingConfig;
using ParseResult = ::Envoy::HeaderRouting::ParseResult;
using ParsedTarget = ::Envoy::HeaderRouting::ParsedTarget;

/**
 * All header_routing network filter stats. @see stats_macros.h
 */
#define ALL_HEADER_ROUTING_TCP_STATS(COUNTER) COUNTER(invalid)

/**
 * Struct definition for all header_routing network filter stats. @see stats_macros.h
 */
struct HeaderRoutingTcpStats {
  ALL_HEADER_ROUTING_TCP_STATS(GENERATE_COUNTER_STRUCT)
};

// 跨连接共享的过滤器配置：协议参数（magic/version）+ 统计。
// 由工厂在配置期构造一次，运行时每条连接构造 filter 时传入。
class HeaderRoutingTcpFilterConfig {
public:
  HeaderRoutingTcpFilterConfig(
      const envoy::extensions::filters::network::header_routing::v3::HeaderRouting& proto_config,
      Server::Configuration::FactoryContext& context);

  const ParserConfig& parserConfig() const { return parser_config_; }
  HeaderRoutingTcpStats& stats() { return stats_; }

private:
  static HeaderRoutingTcpStats generateStats(Stats::Scope& scope) {
    return {ALL_HEADER_ROUTING_TCP_STATS(POOL_COUNTER(scope))};
  }

  ParserConfig parser_config_;
  Stats::ScopeSharedPtr scope_;
  HeaderRoutingTcpStats stats_;
};

using HeaderRoutingTcpFilterConfigSharedPtr = std::shared_ptr<HeaderRoutingTcpFilterConfig>;

// TCP 适配器（Network::ReadFilter）：三步动态路由。
// ① onNewConnection 阻断（阻止 sni_dynamic_forward_proxy 在头部就绪前选上游）
// ② onData 累积字节流 → 解析头部 → 剥离头部 → 写 dynamic_host/port filter state
// ③ continueReading 续链（触发 sni_dynamic_forward_proxy.onNewConnection 读状态并选上游）
class HeaderRoutingTcpFilter : public Network::ReadFilter, Logger::Loggable<Logger::Id::filter> {
public:
  HeaderRoutingTcpFilter(HeaderRoutingTcpFilterConfigSharedPtr config)
      : config_(std::move(config)) {}

  // Network::ReadFilter
  Network::FilterStatus onNewConnection() override;
  Network::FilterStatus onData(Buffer::Instance& data, bool end_stream) override;

  void initializeReadFilterCallbacks(Network::ReadFilterCallbacks& callbacks) override {
    read_callbacks_ = &callbacks;
  }

private:
  // 写入 sni_dynamic_forward_proxy 读取的目标地址 filter state（类型与其读取类型严格一致）。
  void setTargetFilterState(const ParsedTarget& target);

  const HeaderRoutingTcpFilterConfigSharedPtr config_;
  Network::ReadFilterCallbacks* read_callbacks_{};
  // 头部是否已成功解析（TCP 连接最前端只带头一次，每个连接一个 filter 实例）。
  bool header_handled_{false};
};

} // namespace HeaderRouting
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
