#pragma once

#include <cstdint>
#include <optional>
#include <string>

#include "absl/strings/string_view.h"

namespace Envoy {
namespace HeaderRouting {

// 头部常量：8 字节固定头 [Magic 1B][Version 1B][RoomIP 4B][RoomPort 2B 大端]
constexpr size_t HeaderLength = 8;   // 头部总长度（字节）
constexpr size_t MagicOffset = 0;    // Magic 字段偏移
constexpr size_t VersionOffset = 1;  // Version 字段偏移
constexpr size_t IpOffset = 2;       // RoomIP 字段偏移
constexpr size_t PortOffset = 6;     // RoomPort 字段偏移

// 由 UDP/TCP 两个 filter 的 proto 配置各自解析后，转成同一结构共用。
// 未来头部格式变更在此扩展字段，适配器无需改动。
struct HeaderRoutingConfig {
  uint8_t magic{0x55};  // Magic 字节，默认 0x55，防误判
  uint8_t version{1};   // 协议版本，默认 1
  // 是否把 8 字节协议头原封不动转发给上游（UDP/TCP proto 字段 forward_header）：
  //  - true（默认）：Envoy 解析头部仅用于选路，之后保留头部，头部连同游戏数据一起转发；
  //  - false：解析选路后剥离头部，只把游戏数据转发给上游。
  bool forward_header{true};
};

// 解析出的目标地址：直接给"规范化字符串 IP + 端口"，适配器无需再转换。
struct ParsedTarget {
  std::string ip;  // 点分十进制，如 "10.0.0.3"
  uint16_t port;   // 主机序，如 8600
};

// 解析结果：NeedMoreData 统一表达"数据不足头部长度"（UDP 视为畸形弃包，TCP 视为继续累积）。
struct ParseResult {
  enum class Status {
    Ok,           // 解析成功，target 有效
    NeedMoreData, // 数据不足头部长度
    BadMagic,     // Magic 校验失败
    BadVersion,   // Version 不支持
  };
  Status status{Status::NeedMoreData};
  std::optional<ParsedTarget> target;  // status == Ok 时有效
};

// 共享无状态 Parser：纯函数解析，天然线程安全，错误统计归各适配器。
class HeaderParser {
public:
  // 解析输入头部。
  // 语义：data 是"可能含头部的一段字节"；
  //  - 长度不足 HeaderLength → NeedMoreData
  //  - Magic/Version 不符 → BadMagic/BadVersion
  //  - 成功 → Ok + target（IP 已转点分十进制，端口已转主机序）
  static ParseResult parse(absl::string_view data, const HeaderRoutingConfig& config);
};

} // namespace HeaderRouting
} // namespace Envoy
