#include "contrib/stat_sinks/wasm_filter/source/wasm_filter_stat_sink_impl.h"

#include <cstdint>
#include <cstring>
#include <string>

using proxy_wasm::RegisterForeignFunction;
using proxy_wasm::WasmResult;

namespace Envoy {
namespace Extensions {
namespace StatSinks {
namespace WasmFilter {

namespace {

thread_local StatsFilterContext* active_context = nullptr;
thread_local Stats::TagVector* active_global_tags = nullptr;

constexpr size_t kU32 = sizeof(uint32_t);
constexpr size_t kU64 = sizeof(uint64_t);

bool readU32(const char* data, size_t total, size_t& offset, uint32_t& out) {
  if (offset + kU32 > total) {
    return false;
  }
  memcpy(&out, data + offset, kU32); // NOLINT(safe-memcpy)
  offset += kU32;
  return true;
}

bool readU64(const char* data, size_t total, size_t& offset, uint64_t& out) {
  if (offset + kU64 > total) {
    return false;
  }
  memcpy(&out, data + offset, kU64); // NOLINT(safe-memcpy)
  offset += kU64;
  return true;
}

bool readStr(const char* data, size_t total, size_t& offset, std::string& out) {
  uint32_t len = 0;
  if (!readU32(data, total, offset, len)) {
    return false;
  }
  if (offset + len > total) {
    return false;
  }
  out.assign(data + offset, len);
  offset += len;
  return true;
}

bool readIndexBlock(const char* data, size_t total, size_t& offset,
                    absl::flat_hash_set<uint32_t>& dest) {
  uint32_t count = 0;
  if (!readU32(data, total, offset, count)) {
    return false;
  }
  if (count > (total - offset) / kU32) {
    return false;
  }
  dest.reserve(count);
  for (uint32_t i = 0; i < count; ++i) {
    uint32_t idx = 0;
    if (!readU32(data, total, offset, idx)) {
      return false;
    }
    dest.insert(idx);
  }
  return true;
}

bool readTagVector(const char* data, size_t total, size_t& offset, Stats::TagVector& tags) {
  uint32_t tag_count = 0;
  if (!readU32(data, total, offset, tag_count)) {
    return false;
  }
  if (tag_count > (total - offset) / (2 * kU32)) {
    return false;
  }
  tags.reserve(tag_count);
  for (uint32_t t = 0; t < tag_count; ++t) {
    Stats::Tag tag;
    if (!readStr(data, total, offset, tag.name_) || !readStr(data, total, offset, tag.value_)) {
      return false;
    }
    tags.push_back(std::move(tag));
  }
  return true;
}

void writeU32(char* out, size_t& pos, uint32_t v) {
  memcpy(out + pos, &v, kU32); // NOLINT(safe-memcpy)
  pos += kU32;
}

void writeStr(char* out, size_t& pos, const std::string& s) {
  writeU32(out, pos, static_cast<uint32_t>(s.size()));
  if (!s.empty()) {
    memcpy(out + pos, s.data(), s.size()); // NOLINT(safe-memcpy)
    pos += s.size();
  }
}

// ---------------------------------------------------------------------------
// Foreign function: stats_filter_emit
//
// Wire format (packed uint32_t array):
//   [counter_count] [counter_idx_0] ... [counter_idx_N]
//   [gauge_count]   [gauge_idx_0]   ... [gauge_idx_M]
//   [hist_count]    [hist_idx_0]    ... [hist_idx_K]   (optional)
// ---------------------------------------------------------------------------
RegisterForeignFunction registerStatsFilterEmit(
    "stats_filter_emit",
    [](proxy_wasm::WasmBase&, std::string_view arguments,
       const std::function<void*(size_t size)>& /*alloc_result*/) -> WasmResult {
      auto* ctx = active_context;
      if (ctx == nullptr) {
        return WasmResult::InternalFailure;
      }

      const char* data = arguments.data();
      const size_t total = arguments.size();

      if (total < kU32) {
        return WasmResult::BadArgument;
      }

      size_t offset = 0;
      if (!readIndexBlock(data, total, offset, ctx->kept_counter_indices)) {
        return WasmResult::BadArgument;
      }
      if (!readIndexBlock(data, total, offset, ctx->kept_gauge_indices)) {
        return WasmResult::BadArgument;
      }
      if (offset < total) {
        if (!readIndexBlock(data, total, offset, ctx->kept_histogram_indices)) {
          return WasmResult::BadArgument;
        }
        ctx->histogram_block_present = true;
      }

      ctx->emit_called = true;
      return WasmResult::Ok;
    });

// ---------------------------------------------------------------------------
// Foreign function: stats_filter_set_global_tags
//
// Called once during plugin startup (onConfigure) to set tags that will be
// applied to ALL metrics on every flush.
//
// Wire format:
//   [tag_count: u32]
//   For each tag:
//     [name_len: u32] [name bytes] [value_len: u32] [value bytes]
// ---------------------------------------------------------------------------
RegisterForeignFunction registerStatsFilterSetGlobalTags(
    "stats_filter_set_global_tags",
    [](proxy_wasm::WasmBase&, std::string_view arguments,
       const std::function<void*(size_t size)>& /*alloc_result*/) -> WasmResult {
      const char* data = arguments.data();
      const size_t total = arguments.size();
      size_t offset = 0;

      Stats::TagVector* tags = active_global_tags;
      if (tags == nullptr) {
        return WasmResult::InternalFailure;
      }
      tags->clear();
      if (!readTagVector(data, total, offset, *tags)) {
        return WasmResult::BadArgument;
      }

      return WasmResult::Ok;
    });

// ---------------------------------------------------------------------------
// Foreign function: stats_filter_set_name_overrides
//
// Called per flush to rename specific metrics.
//
// Wire format:
//   [count: u32]
//   For each override:
//     [type: u32 (1=counter, 2=gauge, 3=histogram)]
//     [index: u32]
//     [new_name_len: u32] [new_name bytes]
// ---------------------------------------------------------------------------
RegisterForeignFunction registerStatsFilterSetNameOverrides(
    "stats_filter_set_name_overrides",
    [](proxy_wasm::WasmBase&, std::string_view arguments,
       const std::function<void*(size_t size)>& /*alloc_result*/) -> WasmResult {
      auto* ctx = active_context;
      if (ctx == nullptr) {
        return WasmResult::InternalFailure;
      }

      const char* data = arguments.data();
      const size_t total = arguments.size();
      size_t offset = 0;

      uint32_t count = 0;
      if (!readU32(data, total, offset, count)) {
        return WasmResult::BadArgument;
      }
      if (count > (total - offset) / (3 * kU32)) {
        return WasmResult::BadArgument;
      }

      ctx->name_overrides.reserve(count);
      for (uint32_t i = 0; i < count; ++i) {
        NameOverride ovr;
        if (!readU32(data, total, offset, ovr.type) || !readU32(data, total, offset, ovr.index)) {
          return WasmResult::BadArgument;
        }
        if (!readStr(data, total, offset, ovr.new_name)) {
          return WasmResult::BadArgument;
        }
        ctx->name_overrides.push_back(std::move(ovr));
      }

      return WasmResult::Ok;
    });

// ---------------------------------------------------------------------------
// Foreign function: stats_filter_inject_metrics
//
// Called per flush to inject synthetic counters and gauges.
//
// Wire format:
//   [counter_count: u32]
//   For each counter:
//     [name_len: u32] [name bytes] [value: u64]
//     [tag_count: u32] for each tag: [name_len] [name] [value_len] [value]
//   [gauge_count: u32]
//   For each gauge:
//     [name_len: u32] [name bytes] [value: u64]
//     [tag_count: u32] for each tag: [name_len] [name] [value_len] [value]
// ---------------------------------------------------------------------------
RegisterForeignFunction registerStatsFilterInjectMetrics(
    "stats_filter_inject_metrics",
    [](proxy_wasm::WasmBase&, std::string_view arguments,
       const std::function<void*(size_t size)>& /*alloc_result*/) -> WasmResult {
      auto* ctx = active_context;
      if (ctx == nullptr) {
        return WasmResult::InternalFailure;
      }

      const char* data = arguments.data();
      const size_t total = arguments.size();
      size_t offset = 0;

      auto readMetricBlock = [&](std::vector<SyntheticMetricDef>& dest) -> bool {
        uint32_t count = 0;
        if (!readU32(data, total, offset, count)) {
          return false;
        }
        if (count > (total - offset) / (2 * kU32 + kU64)) {
          return false;
        }
        dest.reserve(count);
        for (uint32_t i = 0; i < count; ++i) {
          SyntheticMetricDef def;
          if (!readStr(data, total, offset, def.name)) {
            return false;
          }
          if (!readU64(data, total, offset, def.value)) {
            return false;
          }
          if (!readTagVector(data, total, offset, def.tags)) {
            return false;
          }
          dest.push_back(std::move(def));
        }
        return true;
      };

      if (!readMetricBlock(ctx->synthetic_counters)) {
        return WasmResult::BadArgument;
      }
      if (!readMetricBlock(ctx->synthetic_gauges)) {
        return WasmResult::BadArgument;
      }

      return WasmResult::Ok;
    });

// ---------------------------------------------------------------------------
// Foreign function: stats_filter_get_metric_tags
//
// Input: [type: u32] [index: u32]
// Output: [tag_count: u32] for each: [name_len][name][value_len][value]
// ---------------------------------------------------------------------------
RegisterForeignFunction registerStatsFilterGetMetricTags(
    "stats_filter_get_metric_tags",
    [](proxy_wasm::WasmBase&, std::string_view arguments,
       const std::function<void*(size_t size)>& alloc_result) -> WasmResult {
      auto* ctx = active_context;
      if (ctx == nullptr || ctx->snapshot == nullptr) {
        return WasmResult::InternalFailure;
      }

      if (arguments.size() < 2 * kU32) {
        return WasmResult::BadArgument;
      }

      size_t offset = 0;
      uint32_t type = 0;
      uint32_t index = 0;
      readU32(arguments.data(), arguments.size(), offset, type);
      readU32(arguments.data(), arguments.size(), offset, index);

      Stats::TagVector tags;
      if (type == 1) {
        // Translate buffer-order index to snapshot-order index.
        if (index >= ctx->counter_buffer_to_snapshot.size()) {
          return WasmResult::BadArgument;
        }
        uint32_t snap_idx = ctx->counter_buffer_to_snapshot[index];
        const auto& counters = ctx->snapshot->counters();
        if (snap_idx >= counters.size()) {
          return WasmResult::BadArgument;
        }
        tags = counters[snap_idx].counter_.get().tags();
      } else if (type == 2) {
        if (index >= ctx->gauge_buffer_to_snapshot.size()) {
          return WasmResult::BadArgument;
        }
        uint32_t snap_idx = ctx->gauge_buffer_to_snapshot[index];
        const auto& gauges = ctx->snapshot->gauges();
        if (snap_idx >= gauges.size()) {
          return WasmResult::BadArgument;
        }
        tags = gauges[snap_idx].get().tags();
      } else if (type == 3) {
        const auto& histograms = ctx->snapshot->histograms();
        if (index >= histograms.size()) {
          return WasmResult::BadArgument;
        }
        tags = histograms[index].get().tags();
      } else {
        return WasmResult::BadArgument;
      }

      size_t out_size = kU32;
      for (const auto& tag : tags) {
        out_size += kU32 + tag.name_.size() + kU32 + tag.value_.size();
      }

      auto* out = static_cast<char*>(alloc_result(out_size));
      size_t pos = 0;
      writeU32(out, pos, static_cast<uint32_t>(tags.size()));
      for (const auto& tag : tags) {
        writeStr(out, pos, tag.name_);
        writeStr(out, pos, tag.value_);
      }

      return WasmResult::Ok;
    });

// ---------------------------------------------------------------------------
// Foreign function: stats_filter_get_all_metric_tags
//
// Bulk: returns tags for used counters and gauges (in buffer order) and all histograms.
// Output: 3 blocks (counters, gauges, histograms), each:
//   [metric_count: u32]
//   For each metric: [tag_count: u32] for each tag: [name_len][name][value_len][value]
// ---------------------------------------------------------------------------
RegisterForeignFunction registerStatsFilterGetAllMetricTags(
    "stats_filter_get_all_metric_tags",
    [](proxy_wasm::WasmBase&, std::string_view /*arguments*/,
       const std::function<void*(size_t size)>& alloc_result) -> WasmResult {
      auto* ctx = active_context;
      if (ctx == nullptr || ctx->snapshot == nullptr) {
        return WasmResult::InternalFailure;
      }

      const auto& counters = ctx->snapshot->counters();
      const auto& gauges = ctx->snapshot->gauges();
      const auto& histograms = ctx->snapshot->histograms();

      auto collectBufferedTags = [](const auto& metrics, const auto& buffer_to_snapshot,
                                    auto extract_tags) {
        std::vector<Stats::TagVector> all;
        all.reserve(buffer_to_snapshot.size());
        for (uint32_t snapshot_index : buffer_to_snapshot) {
          all.push_back(extract_tags(metrics[snapshot_index]));
        }
        return all;
      };

      auto counter_tags =
          collectBufferedTags(counters, ctx->counter_buffer_to_snapshot,
                              [](const auto& c) { return c.counter_.get().tags(); });
      auto gauge_tags = collectBufferedTags(gauges, ctx->gauge_buffer_to_snapshot,
                                            [](const auto& g) { return g.get().tags(); });
      std::vector<Stats::TagVector> histogram_tags;
      histogram_tags.reserve(histograms.size());
      for (const auto& h : histograms) {
        histogram_tags.push_back(h.get().tags());
      }

      auto blockSize = [](const std::vector<Stats::TagVector>& tag_groups) -> size_t {
        size_t sz = kU32;
        for (const auto& tags : tag_groups) {
          sz += kU32;
          for (const auto& tag : tags) {
            sz += kU32 + tag.name_.size() + kU32 + tag.value_.size();
          }
        }
        return sz;
      };

      size_t out_size = blockSize(counter_tags) + blockSize(gauge_tags) + blockSize(histogram_tags);
      auto* out = static_cast<char*>(alloc_result(out_size));
      size_t pos = 0;

      auto writeBlock = [&](const std::vector<Stats::TagVector>& tag_groups) {
        writeU32(out, pos, static_cast<uint32_t>(tag_groups.size()));
        for (const auto& tags : tag_groups) {
          writeU32(out, pos, static_cast<uint32_t>(tags.size()));
          for (const auto& tag : tags) {
            writeStr(out, pos, tag.name_);
            writeStr(out, pos, tag.value_);
          }
        }
      };

      writeBlock(counter_tags);
      writeBlock(gauge_tags);
      writeBlock(histogram_tags);

      return WasmResult::Ok;
    });

// ---------------------------------------------------------------------------
// Foreign function: stats_filter_get_histograms
//
// Returns histogram names so the plugin can filter them.
// Output: [count: u32] for each: [name_len: u32] [name bytes]
// ---------------------------------------------------------------------------
RegisterForeignFunction registerStatsFilterGetHistograms(
    "stats_filter_get_histograms",
    [](proxy_wasm::WasmBase&, std::string_view /*arguments*/,
       const std::function<void*(size_t size)>& alloc_result) -> WasmResult {
      auto* ctx = active_context;
      if (ctx == nullptr || ctx->snapshot == nullptr) {
        return WasmResult::InternalFailure;
      }

      const auto& histograms = ctx->snapshot->histograms();

      size_t out_size = kU32;
      for (const auto& hist_ref : histograms) {
        out_size += kU32 + hist_ref.get().name().size();
      }

      auto* out = static_cast<char*>(alloc_result(out_size));
      size_t pos = 0;

      writeU32(out, pos, static_cast<uint32_t>(histograms.size()));
      for (const auto& hist_ref : histograms) {
        writeStr(out, pos, hist_ref.get().name());
      }

      return WasmResult::Ok;
    });

} // namespace

StatsFilterContext* getActiveContext() { return active_context; }
void setActiveContext(StatsFilterContext* ctx) { active_context = ctx; }

Stats::TagVector* getGlobalTags() { return active_global_tags; }
void setGlobalTags(Stats::TagVector* tags) { active_global_tags = tags; }

void buildBufferToSnapshotMaps(Stats::MetricSnapshot& snapshot, StatsFilterContext& ctx) {
  const auto& counters = snapshot.counters();
  ctx.counter_buffer_to_snapshot.reserve(counters.size());
  for (uint32_t i = 0; i < counters.size(); ++i) {
    if (counters[i].counter_.get().used()) {
      ctx.counter_buffer_to_snapshot.push_back(i);
    }
  }

  const auto& gauges = snapshot.gauges();
  ctx.gauge_buffer_to_snapshot.reserve(gauges.size());
  for (uint32_t i = 0; i < gauges.size(); ++i) {
    if (gauges[i].get().used()) {
      ctx.gauge_buffer_to_snapshot.push_back(i);
    }
  }
}

void processFilterDecisionsAndFlush(Stats::MetricSnapshot& snapshot, StatsFilterContext& context,
                                    Stats::TagVector& global_tags, Stats::SymbolTable& symbol_table,
                                    Stats::Sink& inner_sink) {
  auto translateIndices = [](absl::flat_hash_set<uint32_t>& indices,
                             const std::vector<uint32_t>& mapping) {
    absl::flat_hash_set<uint32_t> translated;
    translated.reserve(indices.size());
    for (uint32_t buf_idx : indices) {
      if (buf_idx < mapping.size()) {
        translated.insert(mapping[buf_idx]);
      }
    }
    indices = std::move(translated);
  };

  translateIndices(context.kept_counter_indices, context.counter_buffer_to_snapshot);
  translateIndices(context.kept_gauge_indices, context.gauge_buffer_to_snapshot);

  for (auto& ovr : context.name_overrides) {
    if (ovr.type == 1 && ovr.index < context.counter_buffer_to_snapshot.size()) {
      ovr.index = context.counter_buffer_to_snapshot[ovr.index];
    } else if (ovr.type == 2 && ovr.index < context.gauge_buffer_to_snapshot.size()) {
      ovr.index = context.gauge_buffer_to_snapshot[ovr.index];
    }
  }

  const auto& counters = snapshot.counters();
  const auto& gauges = snapshot.gauges();

  bool has_enrichments = !global_tags.empty() || !context.name_overrides.empty() ||
                         !context.synthetic_counters.empty() || !context.synthetic_gauges.empty();

  if (!context.emit_called && !has_enrichments) {
    inner_sink.flush(snapshot);
    return;
  }

  if (!context.emit_called && has_enrichments) {
    // Plugin didn't filter, but has enrichments - keep all metrics.
    for (uint32_t i = 0; i < counters.size(); ++i) {
      context.kept_counter_indices.insert(i);
    }
    for (uint32_t i = 0; i < gauges.size(); ++i) {
      context.kept_gauge_indices.insert(i);
    }
  }

  if (context.emit_called) {
    // Unused metrics always pass through regardless of filtering.
    for (uint32_t i = 0; i < counters.size(); ++i) {
      if (!counters[i].counter_.get().used()) {
        context.kept_counter_indices.insert(i);
      }
    }
    for (uint32_t i = 0; i < gauges.size(); ++i) {
      if (!gauges[i].get().used()) {
        context.kept_gauge_indices.insert(i);
      }
    }
  }

  EnrichedMetricSnapshot enriched(snapshot, context, global_tags, symbol_table);
  inner_sink.flush(enriched);
}

// ---------------------------------------------------------------------------
// EnrichedMetricSnapshot
// ---------------------------------------------------------------------------

EnrichedMetricSnapshot::EnrichedMetricSnapshot(Stats::MetricSnapshot& original,
                                               const StatsFilterContext& ctx,
                                               const Stats::TagVector& global_tags,
                                               Stats::SymbolTable& symbol_table)
    : original_(original), symbol_table_(symbol_table) {
  const auto& src_counters = original.counters();
  const auto& src_gauges = original.gauges();
  const auto& src_histograms = original.histograms();

  // Build per-type name override lookup.
  counter_name_overrides_.resize(src_counters.size());
  gauge_name_overrides_.resize(src_gauges.size());
  histogram_name_overrides_.resize(src_histograms.size());
  for (const auto& ovr : ctx.name_overrides) {
    if (ovr.type == 1 && ovr.index < counter_name_overrides_.size()) {
      counter_name_overrides_[ovr.index] = ovr.new_name;
    } else if (ovr.type == 2 && ovr.index < gauge_name_overrides_.size()) {
      gauge_name_overrides_[ovr.index] = ovr.new_name;
    } else if (ovr.type == 3 && ovr.index < histogram_name_overrides_.size()) {
      histogram_name_overrides_[ovr.index] = ovr.new_name;
    }
  }

  // Build enriched counters.
  counter_wrappers_.reserve(ctx.kept_counter_indices.size());
  enriched_counters_.reserve(ctx.kept_counter_indices.size());
  for (uint32_t i = 0; i < src_counters.size(); ++i) {
    if (ctx.kept_counter_indices.contains(i)) {
      counter_wrappers_.emplace_back(src_counters[i].counter_.get(), global_tags,
                                     counter_name_overrides_[i]);
      enriched_counters_.push_back({src_counters[i].delta_, counter_wrappers_.back()});
    }
  }

  // Build enriched gauges.
  gauge_wrappers_.reserve(ctx.kept_gauge_indices.size());
  enriched_gauges_.reserve(ctx.kept_gauge_indices.size());
  for (uint32_t i = 0; i < src_gauges.size(); ++i) {
    if (ctx.kept_gauge_indices.contains(i)) {
      gauge_wrappers_.emplace_back(src_gauges[i].get(), global_tags, gauge_name_overrides_[i]);
      enriched_gauges_.emplace_back(gauge_wrappers_.back());
    }
  }

  // Build enriched histograms.
  if (!ctx.histogram_block_present) {
    // No histogram block in emit call -- pass all through with enrichment.
    histogram_wrappers_.reserve(src_histograms.size());
    enriched_histograms_.reserve(src_histograms.size());
    for (uint32_t i = 0; i < src_histograms.size(); ++i) {
      histogram_wrappers_.emplace_back(
          src_histograms[i].get(), global_tags,
          i < histogram_name_overrides_.size() ? histogram_name_overrides_[i] : std::string());
      enriched_histograms_.emplace_back(histogram_wrappers_.back());
    }
  } else {
    histogram_wrappers_.reserve(ctx.kept_histogram_indices.size());
    enriched_histograms_.reserve(ctx.kept_histogram_indices.size());
    for (uint32_t i = 0; i < src_histograms.size(); ++i) {
      if (ctx.kept_histogram_indices.contains(i)) {
        histogram_wrappers_.emplace_back(src_histograms[i].get(), global_tags,
                                         histogram_name_overrides_[i]);
        enriched_histograms_.emplace_back(histogram_wrappers_.back());
      }
    }
  }

  // Append synthetic counters.
  if (!ctx.synthetic_counters.empty()) {
    synthetic_counter_objs_.reserve(ctx.synthetic_counters.size());
    for (const auto& def : ctx.synthetic_counters) {
      auto merged_tags = mergeTags(def.tags, global_tags);
      synthetic_counter_objs_.emplace_back(symbol_table_, def.name, def.value,
                                           std::move(merged_tags));
    }
    for (auto& sc : synthetic_counter_objs_) {
      enriched_counters_.push_back({sc.value(), sc});
    }
  }

  // Append synthetic gauges.
  if (!ctx.synthetic_gauges.empty()) {
    synthetic_gauge_objs_.reserve(ctx.synthetic_gauges.size());
    for (const auto& def : ctx.synthetic_gauges) {
      auto merged_tags = mergeTags(def.tags, global_tags);
      synthetic_gauge_objs_.emplace_back(symbol_table_, def.name, def.value,
                                         std::move(merged_tags));
    }
    for (auto& sg : synthetic_gauge_objs_) {
      enriched_gauges_.emplace_back(sg);
    }
  }
}

// ---------------------------------------------------------------------------
// WasmFilterStatsSink
// ---------------------------------------------------------------------------

WasmFilterStatsSink::WasmFilterStatsSink(Common::Wasm::PluginConfigPtr plugin_config,
                                         Stats::SinkPtr inner_sink,
                                         Stats::SymbolTable& symbol_table,
                                         Stats::TagVector initial_global_tags)
    : plugin_config_(std::move(plugin_config)), inner_sink_(std::move(inner_sink)),
      symbol_table_(symbol_table), global_tags_(std::move(initial_global_tags)) {}

void WasmFilterStatsSink::flush(Stats::MetricSnapshot& snapshot) {
  context_.clear();
  context_.snapshot = &snapshot;

  Common::Wasm::Wasm* wasm = plugin_config_->wasm();
  if (wasm == nullptr) {
    inner_sink_->flush(snapshot);
    return;
  }

  buildBufferToSnapshotMaps(snapshot, context_);

  setActiveContext(&context_);
  setGlobalTags(&global_tags_);

  wasm->onStatsUpdate(plugin_config_->plugin(), snapshot);

  setActiveContext(nullptr);
  setGlobalTags(nullptr);

  processFilterDecisionsAndFlush(snapshot, context_, global_tags_, symbol_table_, *inner_sink_);
}

} // namespace WasmFilter
} // namespace StatSinks
} // namespace Extensions
} // namespace Envoy
