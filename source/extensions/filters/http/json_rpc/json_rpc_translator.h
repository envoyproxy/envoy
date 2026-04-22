#pragma once

#include "source/extensions/filters/http/json_rpc/json_decoder.h"
#include "source/extensions/filters/http/json_rpc/json_rpc_decoder.h"

#include "nlohmann/json.hpp"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace JsonRpc {

/**
 * Second stage of the two-stage HTTP → JSON → JSON-RPC translation pipeline.
 *
 * Implements JsonDecoder and maps the incoming structural JSON events onto
 * JSON-RPC field semantics, then forwards them to a JsonRpcDecoder:
 *
 *   JsonParser  →  JsonRpcTranslator  →  JsonRpcDecoder
 *   (stage 1)       (stage 2)
 *
 * Field mapping:
 *   onObjectBegin              → onJsonRpcBegin
 *   onObjectEnd                → onJsonRpcComplete
 *   onKey("method")            → [remember field = Method]
 *   onStringValue while Method → onMethod(value)
 *   onKey("id")                → [remember field = Id]
 *   onStringValue while Id     → onId(string json node)
 *   onPrimitiveValue while Id  → onId(parsed json node)
 *   onKey("params")            → [remember field = Params]
 *   onNestedValue while Params → onParams(parsed json node)
 *   onKey("jsonrpc")           → [remember field = Jsonrpc — validated, not forwarded]
 *   all other keys/values      → silently ignored
 *   onError                    → onError
 */
class JsonRpcTranslator : public JsonDecoder {
public:
  explicit JsonRpcTranslator(JsonRpcDecoder& decoder) : decoder_(decoder) {}

  void onObjectBegin() override { decoder_.onJsonRpcBegin(); }
  void onObjectEnd() override { decoder_.onJsonRpcComplete(); }

  void onKey(absl::string_view key) override {
    if (key == "method")  { current_field_ = Field::Method;  return; }
    if (key == "id")      { current_field_ = Field::Id;      return; }
    if (key == "params")  { current_field_ = Field::Params;  return; }
    if (key == "jsonrpc") { current_field_ = Field::Jsonrpc; return; }
    current_field_ = Field::Unknown;
  }

  void onStringValue(absl::string_view value) override {
    switch (current_field_) {
    case Field::Method:
      decoder_.onMethod(value);
      break;
    case Field::Id:
      // String id: deliver as a JSON string node.
      decoder_.onId(nlohmann::json(std::string(value)));
      break;
    default:
      break;
    }
    current_field_ = Field::Unknown;
  }

  void onPrimitiveValue(absl::string_view raw) override {
    if (current_field_ == Field::Id) {
      // Number or null id: parse the raw token into a typed JSON node.
      auto parsed = nlohmann::json::parse(raw, /*cb=*/nullptr, /*allow_exceptions=*/false);
      if (!parsed.is_discarded()) {
        decoder_.onId(std::move(parsed));
      }
    }
    current_field_ = Field::Unknown;
  }

  void onNestedValue(absl::string_view raw_json) override {
    if (current_field_ == Field::Params) {
      auto parsed = nlohmann::json::parse(raw_json, /*cb=*/nullptr, /*allow_exceptions=*/false);
      if (!parsed.is_discarded()) {
        decoder_.onParams(std::move(parsed));
      }
    }
    current_field_ = Field::Unknown;
  }

  void onError(absl::string_view message) override { decoder_.onError(message); }

private:
  enum class Field { Unknown, Jsonrpc, Method, Id, Params };

  JsonRpcDecoder& decoder_;
  Field current_field_{Field::Unknown};
};

} // namespace JsonRpc
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
