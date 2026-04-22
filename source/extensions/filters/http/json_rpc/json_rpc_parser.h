#pragma once

#include "envoy/buffer/buffer.h"

#include "source/extensions/filters/http/json_rpc/json_decoder.h"
#include "source/extensions/filters/http/json_rpc/json_parser.h"
#include "source/extensions/filters/http/json_rpc/json_rpc_decoder.h"
#include "source/extensions/filters/http/json_rpc/json_rpc_translator.h"

#include "absl/status/status.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace JsonRpc {

/**
 * Convenience facade composing JsonParser and JsonRpcTranslator.
 *
 * Provides the same API as the original single-class parser so that callers
 * that want both stages wired together do not need to manage them separately.
 *
 *   JsonRpcParser(decoder)
 *     internally owns: JsonRpcTranslator → JsonParser
 *
 * Prefer using JsonParser + JsonRpcTranslator directly when you need access
 * to the intermediate JsonDecoder layer (e.g. for testing or interception).
 */
class JsonRpcParser {
public:
  explicit JsonRpcParser(JsonRpcDecoder& decoder)
      : translator_(decoder), parser_(translator_) {}

  /**
   * Feed one buffer chunk into the parser.
   * Delegates to JsonParser::parse(); may fire JsonRpcDecoder callbacks
   * synchronously before returning.
   */
  absl::Status parse(const Buffer::Instance& data) { return parser_.parse(data); }

  /**
   * Signal end of input and validate completeness.
   * Delegates to JsonParser::finishParse().
   */
  absl::Status finishParse() { return parser_.finishParse(); }

private:
  JsonRpcTranslator translator_;
  JsonParser parser_;
};

} // namespace JsonRpc
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
