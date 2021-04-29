// NOLINT(namespace-envoy)
#pragma once

namespace proxy_wasm {
namespace null_plugin {

#include "proxy_wasm_common.h"
#include "proxy_wasm_enums.h"
#include "proxy_wasm_externs.h"

/*
 * The following headers are used in two different environments, in the Null VM and in Wasm code
 * which require different headers to precede these  such that they can not include the above
 * headers directly. These macros prevent header reordering
 */
#define _THE_FOLLOWING_INCLUDE_MUST_COME_AFTER_THOSE_ABOVE_ 1
#include "proxy_wasm_api.h"
#undef _THE_FOLLOWING_INCLUDE_MUST_COME_AFTER_THOSE_ABOVE_
#define _THE_FOLLOWING_INCLUDE_MUST_COME_AFTER_THOSE_ABOVE_ 1
#include "extensions/common/wasm/ext/envoy_proxy_wasm_api.h"
#undef _THE_FOLLOWING_INCLUDE_MUST_COME_AFTER_THOSE_ABOVE_

} // namespace null_plugin
} // namespace proxy_wasm
