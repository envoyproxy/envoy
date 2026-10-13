// NOLINT(namespace-envoy)
//
// Test WASM stats filter plugin for envoy.stat_sinks.wasm_filter.
// Exercises all foreign functions: filtering, global tags, name overrides,
// histogram filtering, and synthetic metric injection.

#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

#ifndef NULL_PLUGIN
#include "proxy_wasm_intrinsics.h"

#include "source/extensions/common/wasm/ext/envoy_proxy_wasm_api.h"
#else
#include "source/extensions/common/wasm/ext/envoy_null_plugin.h"
#endif

START_WASM_PLUGIN(StatsFilterTestPlugin)

namespace {
WasmResult callForeignFunction(const std::string& name, const char* data, size_t data_size,
                               char** result, size_t* result_size) {
  return proxy_call_foreign_function(name.data(), name.size(), data, data_size, result,
                                     result_size);
}
} // namespace

struct HistogramInfo {
  std::string name;
};

class StatsFilterTestRootContext : public EnvoyRootContext {
public:
  explicit StatsFilterTestRootContext(uint32_t id, std::string_view root_id)
      : EnvoyRootContext(id, root_id) {}

  bool onConfigure(size_t config_size) override;
  void onStatsUpdate(uint32_t result_size) override;

private:
  std::vector<HistogramInfo> fetchHistograms() const;
};

class StatsFilterTestContext : public EnvoyContext {
public:
  explicit StatsFilterTestContext(uint32_t id, RootContext* root) : EnvoyContext(id, root) {}
};

static RegisterContextFactory
    register_StatsFilterTestContext(CONTEXT_FACTORY(StatsFilterTestContext),
                                    ROOT_FACTORY(StatsFilterTestRootContext));

bool StatsFilterTestRootContext::onConfigure(size_t) {
  // Set global tags via stats_filter_set_global_tags.
  std::string wire;
  uint32_t count = 1;
  wire.append(reinterpret_cast<const char*>(&count), sizeof(uint32_t));
  auto appendStr = [&](const std::string& s) {
    uint32_t len = s.size();
    wire.append(reinterpret_cast<const char*>(&len), sizeof(uint32_t));
    wire.append(s);
  };
  appendStr("test_tag");
  appendStr("test_value");

  char* result = nullptr;
  size_t result_size = 0;
  callForeignFunction("stats_filter_set_global_tags", wire.data(), wire.size(), &result,
                      &result_size);
  logInfo("StatsFilterTestPlugin: global tags set");
  return true;
}

std::vector<HistogramInfo> StatsFilterTestRootContext::fetchHistograms() const {
  char* result = nullptr;
  size_t result_size = 0;
  auto status =
      callForeignFunction("stats_filter_get_histograms", nullptr, 0, &result, &result_size);
  if (status != WasmResult::Ok || result == nullptr || result_size < sizeof(uint32_t)) {
    return {};
  }

  std::vector<HistogramInfo> histograms;
  size_t offset = 0;
  uint32_t hist_count = 0;
  memcpy(&hist_count, result + offset, sizeof(uint32_t)); // NOLINT(safe-memcpy)
  offset += sizeof(uint32_t);

  for (uint32_t i = 0; i < hist_count && offset < result_size; ++i) {
    uint32_t name_len = 0;
    memcpy(&name_len, result + offset, sizeof(uint32_t)); // NOLINT(safe-memcpy)
    offset += sizeof(uint32_t);
    histograms.push_back({std::string(result + offset, name_len)});
    offset += name_len;
  }

  free(result); // NOLINT(cppcoreguidelines-no-malloc)
  return histograms;
}

void StatsFilterTestRootContext::onStatsUpdate(uint32_t result_size) {
  auto stats_buffer = getBufferBytes(WasmBufferType::CallData, 0, result_size);
  auto stats = parseStatResults(stats_buffer->view());

  logInfo("StatsFilterTestPlugin: onStatsUpdate counters=" + std::to_string(stats.counters.size()) +
          " gauges=" + std::to_string(stats.gauges.size()));

  // Keep all counters except those starting with "excluded_".
  std::vector<uint32_t> kept_counter_indices;
  for (uint32_t i = 0; i < stats.counters.size(); ++i) {
    std::string name(stats.counters[i].name);
    if (name.substr(0, 9) != "excluded_") {
      kept_counter_indices.push_back(i);
    }
  }

  // Keep all gauges.
  std::vector<uint32_t> kept_gauge_indices;
  for (uint32_t i = 0; i < stats.gauges.size(); ++i) {
    kept_gauge_indices.push_back(i);
  }

  // Fetch and keep all histograms.
  auto histograms = fetchHistograms();
  std::vector<uint32_t> kept_histogram_indices;
  for (uint32_t i = 0; i < histograms.size(); ++i) {
    kept_histogram_indices.push_back(i);
  }

  logInfo("StatsFilterTestPlugin: kept counters=" + std::to_string(kept_counter_indices.size()) +
          " histograms=" + std::to_string(kept_histogram_indices.size()));

  // Set name overrides: rename first kept counter with "envoy." prefix.
  if (!kept_counter_indices.empty()) {
    std::string ovr_wire;
    uint32_t ovr_count = 1;
    ovr_wire.append(reinterpret_cast<const char*>(&ovr_count), sizeof(uint32_t));
    uint32_t type = 1; // counter
    ovr_wire.append(reinterpret_cast<const char*>(&type), sizeof(uint32_t));
    uint32_t idx = kept_counter_indices[0];
    ovr_wire.append(reinterpret_cast<const char*>(&idx), sizeof(uint32_t));
    std::string new_name = "envoy." + std::string(stats.counters[idx].name);
    uint32_t name_len = new_name.size();
    ovr_wire.append(reinterpret_cast<const char*>(&name_len), sizeof(uint32_t));
    ovr_wire.append(new_name);

    char* nr = nullptr;
    size_t nrs = 0;
    callForeignFunction("stats_filter_set_name_overrides", ovr_wire.data(), ovr_wire.size(), &nr,
                        &nrs);
  }

  // Inject one synthetic counter.
  {
    std::string inj_wire;
    auto appendU32 = [&](uint32_t v) {
      inj_wire.append(reinterpret_cast<const char*>(&v), sizeof(uint32_t));
    };
    auto appendU64 = [&](uint64_t v) {
      inj_wire.append(reinterpret_cast<const char*>(&v), sizeof(uint64_t));
    };
    auto appendString = [&](const std::string& s) {
      appendU32(s.size());
      inj_wire.append(s);
    };

    appendU32(1); // 1 synthetic counter
    appendString("wasm_filter.metrics_kept");
    appendU64(static_cast<uint64_t>(kept_counter_indices.size()));
    appendU32(0); // no tags

    appendU32(0); // 0 synthetic gauges

    char* ir = nullptr;
    size_t irs = 0;
    callForeignFunction("stats_filter_inject_metrics", inj_wire.data(), inj_wire.size(), &ir, &irs);
  }

  // Emit kept indices.
  {
    std::vector<uint32_t> wire;
    wire.push_back(static_cast<uint32_t>(kept_counter_indices.size()));
    for (auto idx : kept_counter_indices) {
      wire.push_back(idx);
    }
    wire.push_back(static_cast<uint32_t>(kept_gauge_indices.size()));
    for (auto idx : kept_gauge_indices) {
      wire.push_back(idx);
    }
    wire.push_back(static_cast<uint32_t>(kept_histogram_indices.size()));
    for (auto idx : kept_histogram_indices) {
      wire.push_back(idx);
    }

    char* emit_result = nullptr;
    size_t emit_result_size = 0;
    callForeignFunction("stats_filter_emit", reinterpret_cast<const char*>(wire.data()),
                        wire.size() * sizeof(uint32_t), &emit_result, &emit_result_size);
  }

  logInfo("StatsFilterTestPlugin: emit complete");
}

END_WASM_PLUGIN
