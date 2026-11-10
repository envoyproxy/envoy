#include <stdatomic.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "source/extensions/dynamic_modules/abi/abi.h"

// A cluster module that calls back into Envoy from inside its hooks. The cluster config selects
// what it does, and the test reads the records through the accessors at the end of this file.

typedef enum {
  // Commits a scheduler event from on_cluster_new.
  ModeSchedulerInNew = 1,
  // Calls pre_init_complete from on_cluster_new.
  ModePreInitInNew = 2,
  // Calls pre_init_complete from on_cluster_init.
  ModePreInitInInit = 3,
  // Calls pre_init_complete from on_cluster_scheduled.
  ModePreInitInScheduled = 4,
  // Starts an HTTP callout from on_cluster_new.
  ModeCalloutInNew = 5,
  // Starts an HTTP callout from on_cluster_init and calls pre_init_complete when it is done.
  ModePreInitInCalloutDone = 6,
  // Calls pre_init_complete from on_cluster_server_initialized.
  ModePreInitInServerInitialized = 7,
  // Calls pre_init_complete from on_cluster_drain_started.
  ModePreInitInDrainStarted = 8,
  // Calls pre_init_complete from on_cluster_shutdown.
  ModePreInitInShutdown = 9,
  // Starts an HTTP callout from on_cluster_destroy.
  ModeCalloutInDestroy = 10,
  // Calls run_on_all_workers from on_cluster_new.
  ModeRunOnAllWorkersInNew = 11,
} mode;

static envoy_dynamic_module_type_cluster_scheduler_module_ptr g_scheduler = NULL;
// The Envoy cluster of the last on_cluster_new, for the hooks that only get the module pointer.
static envoy_dynamic_module_type_cluster_envoy_ptr g_cluster_envoy_ptr = NULL;
static uint64_t g_last_scheduled_event_id = 0;
// Set right after the module's pre_init_complete call returns.
static int g_returned_from_pre_init_complete = 0;
static size_t g_http_callout_done_count = 0;
static int g_last_http_callout_result = -1;
static int g_callout_init_result = -1;
static atomic_uint_least64_t g_last_worker_event_id = 0;

static int matches(envoy_dynamic_module_type_envoy_buffer buffer, const char* value) {
  return buffer.length == strlen(value) && memcmp(buffer.ptr, value, buffer.length) == 0;
}

// Calls pre_init_complete when the cluster runs in the given mode, and records that it returned.
static void
pre_init_complete_in_mode(envoy_dynamic_module_type_cluster_envoy_ptr cluster_envoy_ptr,
                          envoy_dynamic_module_type_cluster_module_ptr cluster_module_ptr,
                          mode expected_mode) {
  if ((mode)(uintptr_t)cluster_module_ptr == expected_mode) {
    envoy_dynamic_module_callback_cluster_pre_init_complete(cluster_envoy_ptr);
    g_returned_from_pre_init_complete = 1;
  }
}

static int start_callout(envoy_dynamic_module_type_cluster_envoy_ptr cluster_envoy_ptr) {
  envoy_dynamic_module_type_module_http_header headers[] = {
      {":method", 7, "GET", 3}, {":path", 5, "/", 1}, {"host", 4, "example.com", 11}};
  uint64_t callout_id = 0;
  return (int)envoy_dynamic_module_callback_cluster_http_callout(
      cluster_envoy_ptr, &callout_id,
      (envoy_dynamic_module_type_module_buffer){"callout_cluster", 15}, headers, 3,
      (envoy_dynamic_module_type_module_buffer){NULL, 0}, 1000);
}

envoy_dynamic_module_type_abi_version_module_ptr envoy_dynamic_module_on_program_init(void) {
  return envoy_dynamic_modules_abi_version;
}

envoy_dynamic_module_type_cluster_config_module_ptr envoy_dynamic_module_on_cluster_config_new(
    envoy_dynamic_module_type_cluster_config_envoy_ptr config_envoy_ptr,
    envoy_dynamic_module_type_envoy_buffer name, envoy_dynamic_module_type_envoy_buffer config) {
  (void)config_envoy_ptr;
  (void)name;
  if (matches(config, "scheduler_in_new")) {
    return (envoy_dynamic_module_type_cluster_config_module_ptr)ModeSchedulerInNew;
  }
  if (matches(config, "pre_init_in_new")) {
    return (envoy_dynamic_module_type_cluster_config_module_ptr)ModePreInitInNew;
  }
  if (matches(config, "pre_init_in_init")) {
    return (envoy_dynamic_module_type_cluster_config_module_ptr)ModePreInitInInit;
  }
  if (matches(config, "pre_init_in_scheduled")) {
    return (envoy_dynamic_module_type_cluster_config_module_ptr)ModePreInitInScheduled;
  }
  if (matches(config, "callout_in_new")) {
    return (envoy_dynamic_module_type_cluster_config_module_ptr)ModeCalloutInNew;
  }
  if (matches(config, "pre_init_in_callout_done")) {
    return (envoy_dynamic_module_type_cluster_config_module_ptr)ModePreInitInCalloutDone;
  }
  if (matches(config, "pre_init_in_server_initialized")) {
    return (envoy_dynamic_module_type_cluster_config_module_ptr)ModePreInitInServerInitialized;
  }
  if (matches(config, "pre_init_in_drain_started")) {
    return (envoy_dynamic_module_type_cluster_config_module_ptr)ModePreInitInDrainStarted;
  }
  if (matches(config, "pre_init_in_shutdown")) {
    return (envoy_dynamic_module_type_cluster_config_module_ptr)ModePreInitInShutdown;
  }
  if (matches(config, "callout_in_destroy")) {
    return (envoy_dynamic_module_type_cluster_config_module_ptr)ModeCalloutInDestroy;
  }
  if (matches(config, "run_on_all_workers_in_new")) {
    return (envoy_dynamic_module_type_cluster_config_module_ptr)ModeRunOnAllWorkersInNew;
  }
  return NULL;
}

void envoy_dynamic_module_on_cluster_config_destroy(
    envoy_dynamic_module_type_cluster_config_module_ptr config_module_ptr) {
  (void)config_module_ptr;
}

envoy_dynamic_module_type_cluster_module_ptr envoy_dynamic_module_on_cluster_new(
    envoy_dynamic_module_type_cluster_config_module_ptr config_module_ptr,
    envoy_dynamic_module_type_cluster_envoy_ptr cluster_envoy_ptr) {
  g_last_scheduled_event_id = 0;
  g_returned_from_pre_init_complete = 0;
  g_http_callout_done_count = 0;
  g_last_http_callout_result = -1;
  g_callout_init_result = -1;
  atomic_store(&g_last_worker_event_id, 0);
  g_cluster_envoy_ptr = cluster_envoy_ptr;
  const mode cluster_mode = (mode)(uintptr_t)config_module_ptr;
  if (cluster_mode == ModeSchedulerInNew) {
    g_scheduler = envoy_dynamic_module_callback_cluster_scheduler_new(cluster_envoy_ptr);
    envoy_dynamic_module_callback_cluster_scheduler_commit(g_scheduler, 42);
  } else if (cluster_mode == ModePreInitInNew) {
    envoy_dynamic_module_callback_cluster_pre_init_complete(cluster_envoy_ptr);
    g_returned_from_pre_init_complete = 1;
  } else if (cluster_mode == ModeCalloutInNew) {
    g_callout_init_result = start_callout(cluster_envoy_ptr);
  } else if (cluster_mode == ModeRunOnAllWorkersInNew) {
    envoy_dynamic_module_callback_cluster_run_on_all_workers(cluster_envoy_ptr, 7);
  }
  return (envoy_dynamic_module_type_cluster_module_ptr)config_module_ptr;
}

void envoy_dynamic_module_on_cluster_init(
    envoy_dynamic_module_type_cluster_envoy_ptr cluster_envoy_ptr,
    envoy_dynamic_module_type_cluster_module_ptr cluster_module_ptr) {
  pre_init_complete_in_mode(cluster_envoy_ptr, cluster_module_ptr, ModePreInitInInit);
  if ((mode)(uintptr_t)cluster_module_ptr == ModePreInitInCalloutDone) {
    g_callout_init_result = start_callout(cluster_envoy_ptr);
  }
}

void envoy_dynamic_module_on_cluster_scheduled(
    envoy_dynamic_module_type_cluster_envoy_ptr cluster_envoy_ptr,
    envoy_dynamic_module_type_cluster_module_ptr cluster_module_ptr, uint64_t event_id) {
  g_last_scheduled_event_id = event_id;
  pre_init_complete_in_mode(cluster_envoy_ptr, cluster_module_ptr, ModePreInitInScheduled);
}

void envoy_dynamic_module_on_cluster_server_initialized(
    envoy_dynamic_module_type_cluster_envoy_ptr cluster_envoy_ptr,
    envoy_dynamic_module_type_cluster_module_ptr cluster_module_ptr) {
  pre_init_complete_in_mode(cluster_envoy_ptr, cluster_module_ptr, ModePreInitInServerInitialized);
}

void envoy_dynamic_module_on_cluster_drain_started(
    envoy_dynamic_module_type_cluster_envoy_ptr cluster_envoy_ptr,
    envoy_dynamic_module_type_cluster_module_ptr cluster_module_ptr) {
  pre_init_complete_in_mode(cluster_envoy_ptr, cluster_module_ptr, ModePreInitInDrainStarted);
}

void envoy_dynamic_module_on_cluster_shutdown(
    envoy_dynamic_module_type_cluster_envoy_ptr cluster_envoy_ptr,
    envoy_dynamic_module_type_cluster_module_ptr cluster_module_ptr,
    envoy_dynamic_module_type_event_cb completion_callback, void* completion_context) {
  pre_init_complete_in_mode(cluster_envoy_ptr, cluster_module_ptr, ModePreInitInShutdown);
  completion_callback(completion_context);
}

void envoy_dynamic_module_on_cluster_http_callout_done(
    envoy_dynamic_module_type_cluster_envoy_ptr cluster_envoy_ptr,
    envoy_dynamic_module_type_cluster_module_ptr cluster_module_ptr, uint64_t callout_id,
    envoy_dynamic_module_type_http_callout_result result,
    envoy_dynamic_module_type_envoy_http_header* headers, size_t headers_size,
    envoy_dynamic_module_type_envoy_buffer* body_chunks, size_t body_chunks_size) {
  (void)callout_id;
  (void)headers;
  (void)headers_size;
  (void)body_chunks;
  (void)body_chunks_size;
  g_http_callout_done_count++;
  g_last_http_callout_result = (int)result;
  pre_init_complete_in_mode(cluster_envoy_ptr, cluster_module_ptr, ModePreInitInCalloutDone);
}

void envoy_dynamic_module_on_cluster_destroy(
    envoy_dynamic_module_type_cluster_module_ptr cluster_module_ptr) {
  if ((mode)(uintptr_t)cluster_module_ptr == ModeCalloutInDestroy) {
    g_callout_init_result = start_callout(g_cluster_envoy_ptr);
  }
  if (g_scheduler != NULL) {
    envoy_dynamic_module_callback_cluster_scheduler_delete(g_scheduler);
    g_scheduler = NULL;
  }
}

envoy_dynamic_module_type_cluster_lb_module_ptr envoy_dynamic_module_on_cluster_lb_new(
    envoy_dynamic_module_type_cluster_module_ptr cluster_module_ptr,
    envoy_dynamic_module_type_cluster_lb_envoy_ptr lb_envoy_ptr) {
  (void)cluster_module_ptr;
  (void)lb_envoy_ptr;
  return (envoy_dynamic_module_type_cluster_lb_module_ptr)0x3;
}

void envoy_dynamic_module_on_cluster_worker_event(
    envoy_dynamic_module_type_cluster_envoy_ptr cluster_envoy_ptr,
    envoy_dynamic_module_type_cluster_module_ptr cluster_module_ptr, uint64_t event_id) {
  (void)cluster_envoy_ptr;
  (void)cluster_module_ptr;
  atomic_store(&g_last_worker_event_id, event_id);
}

void envoy_dynamic_module_on_cluster_lb_destroy(
    envoy_dynamic_module_type_cluster_lb_module_ptr lb_module_ptr) {
  (void)lb_module_ptr;
}

void envoy_dynamic_module_on_cluster_lb_choose_host(
    envoy_dynamic_module_type_cluster_lb_module_ptr lb_module_ptr,
    envoy_dynamic_module_type_cluster_lb_context_envoy_ptr context_envoy_ptr,
    envoy_dynamic_module_type_cluster_host_envoy_ptr* host_out,
    envoy_dynamic_module_type_cluster_lb_async_handle_module_ptr* async_handle_out) {
  (void)lb_module_ptr;
  (void)context_envoy_ptr;
  *host_out = NULL;
  *async_handle_out = NULL;
}

uint64_t cluster_init_in_hooks_test_last_scheduled_event_id(void) {
  return g_last_scheduled_event_id;
}

int cluster_init_in_hooks_test_returned_from_pre_init_complete(void) {
  return g_returned_from_pre_init_complete;
}

size_t cluster_init_in_hooks_test_http_callout_done_count(void) {
  return g_http_callout_done_count;
}

int cluster_init_in_hooks_test_last_http_callout_result(void) { return g_last_http_callout_result; }

int cluster_init_in_hooks_test_callout_init_result(void) { return g_callout_init_result; }

uint64_t cluster_init_in_hooks_test_last_worker_event_id(void) {
  return atomic_load(&g_last_worker_event_id);
}
