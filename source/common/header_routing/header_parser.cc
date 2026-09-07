#include "source/common/header_routing/header_parser.h"

#include "absl/strings/str_cat.h"

namespace Envoy {
namespace HeaderRouting {

ParseResult HeaderParser::parse(absl::string_view data, const HeaderRoutingConfig& config) {
  // 长度不足头部 → 无法解析（UDP 弃包 / TCP 继续累积）。
  if (data.size() < HeaderLength) {
    return ParseResult{ParseResult::Status::NeedMoreData, std::nullopt};
  }

  // 按字节无符号读取，避免 char 符号扩展导致的高位字节误判。
  const auto* bytes = reinterpret_cast<const uint8_t*>(data.data());

  // Magic 防误判。
  if (bytes[MagicOffset] != config.magic) {
    return ParseResult{ParseResult::Status::BadMagic, std::nullopt};
  }

  // Version 协议版本校验。
  if (bytes[VersionOffset] != config.version) {
    return ParseResult{ParseResult::Status::BadVersion, std::nullopt};
  }

  ParsedTarget target;
  // RoomIP：4 字节网络序（大端）转点分十进制。
  target.ip = absl::StrCat(static_cast<uint32_t>(bytes[IpOffset + 0]), ".",
                           static_cast<uint32_t>(bytes[IpOffset + 1]), ".",
                           static_cast<uint32_t>(bytes[IpOffset + 2]), ".",
                           static_cast<uint32_t>(bytes[IpOffset + 3]));
  // RoomPort：2 字节大端转主机序。
  target.port = static_cast<uint16_t>((static_cast<uint16_t>(bytes[PortOffset]) << 8) |
                                      static_cast<uint16_t>(bytes[PortOffset + 1]));
  return ParseResult{ParseResult::Status::Ok, std::move(target)};
}

} // namespace HeaderRouting
} // namespace Envoy
