// NOLINT(namespace-envoy)
#pragma once

// Note that this file is included in emscripten and NullVM environments and thus depends on
// the context in which it is included, hence we need to disable clang-tidy warnings.

extern "C" WasmResult envoy_resolve_dns(const char* dns_address, size_t dns_address_size,
                                        uint32_t* token);

class EnvoyContextBase {
public:
  virtual ~EnvoyContextBase() = default;
};

class EnvoyRootContext : public RootContext, public EnvoyContextBase {
public:
  EnvoyRootContext(uint32_t id, std::string_view root_id) : RootContext(id, root_id) {}
  ~EnvoyRootContext() override = default;

  virtual void onResolveDns(uint32_t /* token */, uint32_t /* result_size */) {}
  virtual void onStatsUpdate(uint32_t /* result_size */) {}
};

class EnvoyContext : public Context, public EnvoyContextBase {
public:
  EnvoyContext(uint32_t id, RootContext* root) : Context(id, root) {}
  ~EnvoyContext() override = default;
};

struct DnsResult {
  uint32_t ttl_seconds;
  std::string address;
};

struct CounterResult {
  uint64_t delta;
  std::string_view name;
  uint64_t value;
};

struct GaugeResult {
  uint64_t value;
  std::string_view name;
};

struct StatResult {
  std::vector<CounterResult> counters;
  std::vector<GaugeResult> gauges;
};

enum class StatType : uint32_t {
  Counter = 1,
  Gauge = 2,
};

inline std::vector<DnsResult> parseDnsResults(std::string_view data) {
  if (data.size() < 4) {
    return {};
  }
  const uint32_t* pn = reinterpret_cast<const uint32_t*>(data.data());
  uint32_t n = *pn++;
  std::vector<DnsResult> results;
  results.resize(n);
  const char* pa = data.data() + (1 + n) * sizeof(uint32_t); // skip n + n TTLs
  for (uint32_t i = 0; i < n; i++) {
    auto& e = results[i];
    e.ttl_seconds = *pn++;
    auto alen = strlen(pa);
    e.address.assign(pa, alen);
    pa += alen + 1;
  }
  return results;
}

template <typename I> inline uint32_t align(uint32_t i) {
  return (i + sizeof(I) - 1) & ~(sizeof(I) - 1);
}

inline StatResult parseStatResults(std::string_view data) {
  StatResult results;
  uint32_t data_len = 0;
  while (data_len < data.length()) {
    const uint32_t* n = reinterpret_cast<const uint32_t*>(data.data() + data_len);
    uint32_t block_size = *n++;
    uint32_t block_type = *n++;
    uint32_t num_stats = *n++;
    if (static_cast<StatType>(block_type) == StatType::Counter) { // counter
      std::vector<CounterResult> counters(num_stats);
      uint32_t stat_index = data_len + 3 * sizeof(uint32_t);
      for (uint32_t i = 0; i < num_stats; i++) {
        const uint32_t* stat_name = reinterpret_cast<const uint32_t*>(data.data() + stat_index);
        uint32_t name_len = *stat_name;
        stat_index += sizeof(uint32_t);

        auto& e = counters[i];
        e.name = {data.data() + stat_index, name_len};
        stat_index = align<uint64_t>(stat_index + name_len);

        const uint64_t* stat_vals = reinterpret_cast<const uint64_t*>(data.data() + stat_index);
        e.value = *stat_vals++;
        e.delta = *stat_vals++;

        stat_index += 2 * sizeof(uint64_t);
      }
      results.counters = counters;
    } else if (static_cast<StatType>(block_type) == StatType::Gauge) { // gauge
      std::vector<GaugeResult> gauges(num_stats);
      uint32_t stat_index = data_len + 3 * sizeof(uint32_t);
      for (uint32_t i = 0; i < num_stats; i++) {
        const uint32_t* stat_name = reinterpret_cast<const uint32_t*>(data.data() + stat_index);
        uint32_t name_len = *stat_name;
        stat_index += sizeof(uint32_t);

        auto& e = gauges[i];
        e.name = {data.data() + stat_index, name_len};
        stat_index = align<uint64_t>(stat_index + name_len);

        const uint64_t* stat_vals = reinterpret_cast<const uint64_t*>(data.data() + stat_index);
        e.value = *stat_vals++;

        stat_index += sizeof(uint64_t);
      }
      results.gauges = gauges;
    }
    data_len += block_size;
  }

  return results;
}

extern "C" WasmResult envoy_resolve_dns(const char* address, size_t address_size, uint32_t* token);
