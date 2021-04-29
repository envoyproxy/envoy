// NOLINT(namespace-envoy)
#include <string>

#ifndef NULL_PLUGIN
#include "proxy_wasm_intrinsics.h"
#else
#include "include/proxy-wasm/null_plugin.h"
#endif

template <typename T> std::unique_ptr<T> wrap_unique(T* ptr) { return std::unique_ptr<T>(ptr); }

START_WASM_PLUGIN(WasmStatsCpp)

// Required Proxy-Wasm ABI version.
WASM_EXPORT(void, proxy_abi_version_0_1_0, ()) {}

// Test the low level interface.
WASM_EXPORT(uint32_t, proxy_on_configure, (uint32_t, uint32_t)) {
  uint32_t c, g, h;
  CHECK_RESULT(defineMetric(MetricType::Counter, "test_counter", &c));
  CHECK_RESULT(defineMetric(MetricType::Gauge, "test_gauge", &g));
  CHECK_RESULT(defineMetric(MetricType::Histogram, "test_histogram", &h));

  CHECK_RESULT(incrementMetric(c, 1));
  CHECK_RESULT(recordMetric(g, 2));
  CHECK_RESULT(recordMetric(h, 3));

  uint64_t value;
  CHECK_RESULT(getMetric(c, &value));
  logTrace(std::string("get counter = ") + std::to_string(value));
  CHECK_RESULT(incrementMetric(c, 1));
  CHECK_RESULT(getMetric(c, &value));
  logDebug(std::string("get counter = ") + std::to_string(value));
  CHECK_RESULT(recordMetric(c, 3));
  CHECK_RESULT(getMetric(c, &value));
  logInfo(std::string("get counter = ") + std::to_string(value));
  CHECK_RESULT(getMetric(g, &value));
  logWarn(std::string("get gauge = ") + std::to_string(value));
  // Get on histograms is not supported.
  if (getMetric(h, &value) != WasmResult::Ok) {
    logError(std::string("get histogram = Unsupported"));
  }
  return 1;
}

// Test the higher level interface.
WASM_EXPORT(void, proxy_on_tick, (uint32_t)) {
  Metric c(MetricType::Counter, "test_counter",
           {MetricTag{"counter_tag", MetricTag::TagType::String}});
  Metric g(MetricType::Gauge, "test_gauge", {MetricTag{"gauge_int_tag", MetricTag::TagType::Int}});
  Metric h(MetricType::Histogram, "test_histogram",
           {MetricTag{"histogram_int_tag", MetricTag::TagType::Int},
            MetricTag{"histogram_string_tag", MetricTag::TagType::String},
            MetricTag{"histogram_bool_tag", MetricTag::TagType::Bool}});

  c.increment(1, "test_tag");
  g.record(2, 9);
  h.record(3, 7, "test_tag", true);

  logTrace(std::string("get counter = ") + std::to_string(c.get("test_tag")));
  c.increment(1, "test_tag");
  logDebug(std::string("get counter = ") + std::to_string(c.get("test_tag")));
  c.record(3, "test_tag");
  logInfo(std::string("get counter = ") + std::to_string(c.get("test_tag")));
  logWarn(std::string("get gauge = ") + std::to_string(g.get(9)));

  auto hh = h.partiallyResolve(7);
  auto h_id = hh.resolve("test_tag", true);
  logError(std::string("resolved histogram name = ") + hh.nameFromIdSlow(h_id));
}

// Test the high level interface.
WASM_EXPORT(void, proxy_on_log, (uint32_t /* context_zero */)) {
  auto c = wrap_unique(
      Counter<std::string, int, bool>::New("test_counter", "string_tag", "int_tag", "bool_tag"));
  auto g =
      wrap_unique(Gauge<std::string, std::string>::New("test_gauge", "string_tag1", "string_tag2"));
  auto h = wrap_unique(Histogram<int, std::string, bool>::New("test_histogram", "int_tag",
                                                              "string_tag", "bool_tag"));

  c->increment(1, "test_tag", 7, true);
  logTrace(std::string("get counter = ") + std::to_string(c->get("test_tag", 7, true)));
  auto simple_c = c->resolve("test_tag", 7, true);
  simple_c++;
  logDebug(std::string("get counter = ") + std::to_string(c->get("test_tag", 7, true)));
  c->record(3, "test_tag", 7, true);
  logInfo(std::string("get counter = ") + std::to_string(c->get("test_tag", 7, true)));

  g->record(2, "test_tag1", "test_tag2");
  logWarn(std::string("get gauge = ") + std::to_string(g->get("test_tag1", "test_tag2")));

  h->record(3, 7, "test_tag", true);
  auto base_h = wrap_unique(Counter<int>::New("test_histogram", "int_tag"));
  auto complete_h =
      wrap_unique(base_h->extendAndResolve<std::string, bool>(7, "string_tag", "bool_tag"));
  auto simple_h = complete_h->resolve("test_tag", true);
  logError(std::string("h_id = ") + complete_h->nameFromIdSlow(simple_h.metric_id));

  Counter<std::string, int, bool> stack_c("test_counter", "string_tag", "int_tag", "bool_tag");
  stack_c.increment(1, "test_tag_stack", 7, true);
  logError(std::string("stack_c = ") + std::to_string(stack_c.get("test_tag_stack", 7, true)));

  Gauge<std::string, std::string> stack_g("test_gauge", "string_tag1", "string_tag2");
  stack_g.record(2, "stack_test_tag1", "test_tag2");
  logError(std::string("stack_g = ") + std::to_string(stack_g.get("stack_test_tag1", "test_tag2")));

  std::string_view int_tag = "int_tag";
  Histogram<int, std::string, bool> stack_h("test_histogram", int_tag, "string_tag", "bool_tag");
  std::string_view stack_test_tag = "stack_test_tag";
  stack_h.record(3, 7, stack_test_tag, true);
}

END_WASM_PLUGIN
