#pragma once

namespace Envoy {
namespace Extensions {
namespace Common {
namespace Wasm {

extern thread_local Envoy::Extensions::Common::Wasm::Context* current_context_;

namespace Null {
namespace Plugin {
class RootContext;
}

Plugin::RootContext* nullVmGetRoot(absl::string_view root_id);

namespace Plugin {

#define WS(_x) Word(static_cast<uint64_t>(_x))
#define WR(_x) Word(reinterpret_cast<uint64_t>(_x))

inline WasmResult wordToWasmResult(Word w) { return static_cast<WasmResult>(w.u64_); }

inline WasmResult proxy_get_configuration(const char** ptr, size_t* size) {
  return wordToWasmResult(Exports::get_configuration(current_context_, WR(ptr), WR(size)));
}

// Logging
inline WasmResult proxy_log(LogLevel level, const char* logMessage, size_t messageSize) {
  return wordToWasmResult(
      Exports::log(current_context_, WS(level), WR(logMessage), WS(messageSize)));
}

// Timer
inline WasmResult proxy_get_current_time_nanoseconds(uint64_t* result) {
  return wordToWasmResult(Exports::get_current_time_nanoseconds(current_context_, WR(result)));
}

// Metrics
// Returns a metric_id which can be used to report a metric. On error returns 0.
inline WasmResult proxy_define_metric(MetricType type, const char* name_ptr, size_t name_size,
                                      uint32_t* metric_id) {
  return wordToWasmResult(Exports::define_metric(current_context_, WS(type), WR(name_ptr),
                                                 WS(name_size), WR(metric_id)));
}
inline WasmResult proxy_increment_metric(uint32_t metric_id, int64_t offset) {
  return wordToWasmResult(Exports::increment_metric(current_context_, WS(metric_id), offset));
}
inline WasmResult proxy_record_metric(uint32_t metric_id, uint64_t value) {
  return wordToWasmResult(Exports::record_metric(current_context_, WS(metric_id), value));
}
inline WasmResult proxy_get_metric(uint32_t metric_id, uint64_t* value) {
  return wordToWasmResult(Exports::get_metric(current_context_, WS(metric_id), WR(value)));
}
inline WasmResult proxy_set_effective_context(uint64_t context_id) {
  return wordToWasmResult(Exports::set_effective_context(current_context_, WS(context_id)));
}
inline WasmResult proxy_done() { return wordToWasmResult(Exports::done(current_context_)); }

#undef WS
#undef WR

inline RootContext* getRoot(StringView root_id) { return nullVmGetRoot(root_id); }

} // namespace Plugin
} // namespace Null
} // namespace Wasm
} // namespace Common
} // namespace Extensions
} // namespace Envoy
