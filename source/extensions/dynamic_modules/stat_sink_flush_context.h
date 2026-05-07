#pragma once

#include <string>
#include <vector>

#include "envoy/stats/sink.h"

namespace Envoy {
namespace Extensions {
namespace DynamicModules {

/**
 * Context passed to a dynamic module during a stats sink flush. It holds a
 * pointer to the live MetricSnapshot along with caches of counter/gauge/
 * text-readout names (and text-readout values), materialized up front because
 * Metric::name() and TextReadout::value() return std::string by value and the
 * ABI needs to hand the module pointers that remain valid for the whole call.
 *
 * An instance is constructed by DynamicModuleStatsSink::flush(), passed to the
 * module via a void* "snapshot" handle, and destroyed when flush() returns.
 * The ABI callbacks in source/extensions/dynamic_modules/abi_impl.cc cast the
 * handle back to this type to serve snapshot reads.
 *
 * Lives in the core dynamic_modules package so that the ABI callbacks are
 * provided by every Envoy binary, independent of which extensions are linked.
 * Without this, any dynamic module that references the snapshot callbacks
 * (e.g. every Go module, since the Go runtime checks undefined symbols at
 * dlopen time) would fail to load in binaries that omit the stats sink.
 */
struct StatSinkFlushContext {
  Stats::MetricSnapshot* snapshot;
  std::vector<std::string> counter_names;
  std::vector<std::string> gauge_names;
  std::vector<std::string> text_readout_names;
  std::vector<std::string> text_readout_values;
};

} // namespace DynamicModules
} // namespace Extensions
} // namespace Envoy
