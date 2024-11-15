// NOLINT(namespace-envoy)
#pragma once

#define PROXY_WASM_PROTOBUF 1
#define PROXY_WASM_PROTOBUF_FULL 1

#include "envoy/config/core/v3/grpc_service.pb.h"

#include "source/extensions/common/wasm/ext/declare_property.pb.h"
#include "source/extensions/common/wasm/ext/verify_signature.pb.h"

#include "include/proxy-wasm/null_plugin.h"

namespace Envoy {
namespace Extensions {
namespace Common {
namespace Wasm {

proxy_wasm::Word resolve_dns(proxy_wasm::Word dns_address, proxy_wasm::Word dns_address_size,
                             proxy_wasm::Word token_ptr);

} // namespace Wasm
} // namespace Common
} // namespace Extensions
} // namespace Envoy

namespace proxy_wasm {
namespace null_plugin {

#include "source/extensions/common/wasm/ext/envoy_proxy_wasm_api.h"
using GrpcService = envoy::config::core::v3::GrpcService;
using namespace proxy_wasm::null_plugin;

#define WS(_x) Word(static_cast<uint64_t>(_x))
#define WR(_x) Word(reinterpret_cast<uint64_t>(_x))

inline WasmResult envoy_resolve_dns(const char* dns_address, size_t dns_address_size,
                                    uint32_t* token) {
  return static_cast<WasmResult>(
      ::Envoy::Extensions::Common::Wasm::resolve_dns(WR(dns_address),
                                                     WS(dns_address_size), WR(token))
          .u64_);
}

#undef WS
#undef WR

} // namespace null_plugin
} // namespace proxy_wasm
