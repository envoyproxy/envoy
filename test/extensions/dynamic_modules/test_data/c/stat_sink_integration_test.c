#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "source/extensions/dynamic_modules/abi/abi.h"

// A stats sink test module whose lifecycle and event hooks emit observable log
// messages via envoy_dynamic_module_callback_log. Integration tests grep Envoy's
// log output for these markers to verify the sink was loaded, configured,
// flushed at least once, and received histogram observations.

static void log_info(const char* message) {
  envoy_dynamic_module_type_module_buffer buf;
  buf.ptr = message;
  buf.length = strlen(message);
  envoy_dynamic_module_callback_log(envoy_dynamic_module_type_log_level_Info, buf);
}

envoy_dynamic_module_type_abi_version_module_ptr envoy_dynamic_module_on_program_init(void) {
  return envoy_dynamic_modules_abi_version;
}

envoy_dynamic_module_type_stat_sink_config_module_ptr
envoy_dynamic_module_on_stat_sink_config_new(
    envoy_dynamic_module_type_stat_sink_config_envoy_ptr config_envoy_ptr,
    envoy_dynamic_module_type_envoy_buffer sink_name,
    envoy_dynamic_module_type_envoy_buffer sink_config) {
  (void)config_envoy_ptr;
  (void)sink_name;
  (void)sink_config;
  log_info("stat sink integration test: config_new called");
  static int config_dummy = 0;
  return &config_dummy;
}

void envoy_dynamic_module_on_stat_sink_config_destroy(
    envoy_dynamic_module_type_stat_sink_config_module_ptr config_module_ptr) {
  (void)config_module_ptr;
  log_info("stat sink integration test: config_destroy called");
}

void envoy_dynamic_module_on_stat_sink_flush(
    envoy_dynamic_module_type_stat_sink_config_module_ptr config_module_ptr,
    envoy_dynamic_module_type_stat_sink_snapshot_envoy_ptr snapshot_envoy_ptr) {
  (void)config_module_ptr;

  // Exercise the snapshot read-back callbacks to prove they are usable during
  // flush. The specific counts depend on the test setup, so we just log them.
  const size_t counter_count =
      envoy_dynamic_module_callback_stat_sink_snapshot_get_counter_count(snapshot_envoy_ptr);
  const size_t gauge_count =
      envoy_dynamic_module_callback_stat_sink_snapshot_get_gauge_count(snapshot_envoy_ptr);

  // Decode each name straight into a stack buffer to exercise the module-provided
  // buffer API. Names longer than the buffer are simply truncated here.
  char name_buf[256];
  size_t name_size = 0;
  uint64_t value = 0;
  uint64_t delta = 0;
  for (size_t i = 0; i < counter_count; i++) {
    envoy_dynamic_module_callback_stat_sink_snapshot_get_counter(
        snapshot_envoy_ptr, i, name_buf, sizeof(name_buf), &name_size, &value, &delta);
  }

  // Look for the always-present "server.uptime" gauge to prove a name round-trips through the buffer
  // API. A small stack buffer is tried first, then a heap retry when the name does not fit, which
  // exercises the truncation-and-retry contract.
  static const char kUptime[] = "server.uptime";
  const size_t uptime_len = sizeof(kUptime) - 1;
  int found_uptime = 0;
  for (size_t i = 0; i < gauge_count; i++) {
    char small[8];
    size_t needed = 0;
    if (!envoy_dynamic_module_callback_stat_sink_snapshot_get_gauge(
            snapshot_envoy_ptr, i, small, sizeof(small), &needed, &value)) {
      continue;
    }
    const char* full = small;
    char* heap = NULL;
    if (needed > sizeof(small)) {
      heap = (char*)malloc(needed);
      if (heap == NULL) {
        continue;
      }
      size_t heap_size = 0;
      envoy_dynamic_module_callback_stat_sink_snapshot_get_gauge(snapshot_envoy_ptr, i, heap, needed,
                                                                 &heap_size, &value);
      full = heap;
    }
    if (needed == uptime_len && memcmp(full, kUptime, uptime_len) == 0) {
      found_uptime = 1;
    }
    free(heap); // free(NULL) is a no-op.
  }

  // Read back any text readouts to exercise the dual-buffer (name and value) getter during flush.
  const size_t text_readout_count =
      envoy_dynamic_module_callback_stat_sink_snapshot_get_text_readout_count(snapshot_envoy_ptr);
  char tr_name[256];
  char tr_value[256];
  size_t tr_name_size = 0;
  size_t tr_value_size = 0;
  for (size_t i = 0; i < text_readout_count; i++) {
    envoy_dynamic_module_callback_stat_sink_snapshot_get_text_readout(
        snapshot_envoy_ptr, i, tr_name, sizeof(tr_name), &tr_name_size, tr_value, sizeof(tr_value),
        &tr_value_size);
  }

  if (found_uptime) {
    log_info("stat sink integration test: found gauge server.uptime");
  }

  char buf[128];
  int n = snprintf(buf, sizeof(buf),
                   "stat sink integration test: flush called counters=%zu gauges=%zu",
                   counter_count, gauge_count);
  if (n < 0 || (size_t)n >= sizeof(buf)) {
    log_info("stat sink integration test: flush called");
    return;
  }
  envoy_dynamic_module_type_module_buffer log_buf;
  log_buf.ptr = buf;
  log_buf.length = (size_t)n;
  envoy_dynamic_module_callback_log(envoy_dynamic_module_type_log_level_Info, log_buf);
}

void envoy_dynamic_module_on_stat_sink_on_histogram_complete(
    envoy_dynamic_module_type_stat_sink_config_module_ptr config_module_ptr,
    envoy_dynamic_module_type_envoy_buffer histogram_name, uint64_t value) {
  (void)config_module_ptr;
  (void)value;
  // The integration test matches on the prefix plus the histogram name passed
  // through from Envoy.
  char buf[256];
  int name_len = (int)histogram_name.length;
  if (name_len > 200) {
    name_len = 200;
  }
  int n = snprintf(buf, sizeof(buf),
                   "stat sink integration test: histogram complete: %.*s",
                   name_len, histogram_name.ptr);
  if (n < 0 || (size_t)n >= sizeof(buf)) {
    log_info("stat sink integration test: histogram complete");
    return;
  }
  envoy_dynamic_module_type_module_buffer log_buf;
  log_buf.ptr = buf;
  log_buf.length = (size_t)n;
  envoy_dynamic_module_callback_log(envoy_dynamic_module_type_log_level_Info, log_buf);
}
