#include <stdatomic.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "source/extensions/dynamic_modules/abi/abi.h"

// A bootstrap extension that implements the optional hooks and records their calls. The test
// reads the records through the accessors at the end of this file. With the config
// "run_on_all_workers_at_startup", it posts a worker event from on_server_initialized, signals
// that its initialization is complete from a later scheduler event, and counts the worker events
// in the worker_events_total counter.

static envoy_dynamic_module_type_bootstrap_extension_config_envoy_ptr g_config_envoy_ptr = NULL;
static envoy_dynamic_module_type_bootstrap_extension_config_scheduler_module_ptr g_scheduler = NULL;
static int g_run_on_all_workers_at_startup = 0;
static size_t g_worker_events_counter_id = 0;
static atomic_size_t g_worker_event_count = 0;
static atomic_uint_least64_t g_last_worker_event_id = 0;
static size_t g_done_count = 0;
static uint64_t g_last_done_event_id = 0;
static size_t g_update_done_count = 0;
static uint64_t g_last_update_id = 0;
static int g_last_update_status = -1;
static char g_last_update_cluster_name[256];
static char g_last_update_error_details[1024];
static size_t g_listener_update_done_count = 0;
static uint64_t g_last_listener_update_id = 0;
static int g_last_listener_update_status = -1;
static char g_last_listener_update_name[256];
static char g_last_listener_update_error_details[1024];
static size_t g_http_callout_done_count = 0;
static int g_last_http_callout_result = -1;

static void copyBuffer(char* destination, size_t destination_size,
                       envoy_dynamic_module_type_envoy_buffer buffer) {
  size_t length = buffer.length < destination_size - 1 ? buffer.length : destination_size - 1;
  if (length > 0) {
    memcpy(destination, buffer.ptr, length);
  }
  destination[length] = '\0';
}

envoy_dynamic_module_type_abi_version_module_ptr envoy_dynamic_module_on_program_init(void) {
  return envoy_dynamic_modules_abi_version;
}

envoy_dynamic_module_type_bootstrap_extension_config_module_ptr
envoy_dynamic_module_on_bootstrap_extension_config_new(
    envoy_dynamic_module_type_bootstrap_extension_config_envoy_ptr extension_config_envoy_ptr,
    envoy_dynamic_module_type_envoy_buffer name, envoy_dynamic_module_type_envoy_buffer config) {
  (void)name;
  static const char kRunOnAllWorkersAtStartup[] = "run_on_all_workers_at_startup";
  g_config_envoy_ptr = extension_config_envoy_ptr;
  g_run_on_all_workers_at_startup =
      config.length == sizeof(kRunOnAllWorkersAtStartup) - 1 &&
      memcmp(config.ptr, kRunOnAllWorkersAtStartup, config.length) == 0;
  if (g_run_on_all_workers_at_startup) {
    envoy_dynamic_module_callback_bootstrap_extension_config_define_counter(
        extension_config_envoy_ptr,
        (envoy_dynamic_module_type_module_buffer){"worker_events_total", 19}, NULL, 0,
        &g_worker_events_counter_id);
    g_scheduler = envoy_dynamic_module_callback_bootstrap_extension_config_scheduler_new(
        extension_config_envoy_ptr);
  }
  atomic_store(&g_worker_event_count, 0);
  atomic_store(&g_last_worker_event_id, 0);
  g_done_count = 0;
  g_last_done_event_id = 0;
  g_update_done_count = 0;
  g_last_update_id = 0;
  g_last_update_status = -1;
  g_last_update_cluster_name[0] = '\0';
  g_last_update_error_details[0] = '\0';
  g_listener_update_done_count = 0;
  g_last_listener_update_id = 0;
  g_last_listener_update_status = -1;
  g_last_listener_update_name[0] = '\0';
  g_last_listener_update_error_details[0] = '\0';
  g_http_callout_done_count = 0;
  g_last_http_callout_result = -1;
  if (!g_run_on_all_workers_at_startup) {
    envoy_dynamic_module_callback_bootstrap_extension_config_signal_init_complete(
        extension_config_envoy_ptr);
  }
  return (envoy_dynamic_module_type_bootstrap_extension_config_module_ptr)0x1;
}

void envoy_dynamic_module_on_bootstrap_extension_config_destroy(
    envoy_dynamic_module_type_bootstrap_extension_config_module_ptr extension_config_ptr) {
  (void)extension_config_ptr;
  if (g_scheduler != NULL) {
    envoy_dynamic_module_callback_bootstrap_extension_config_scheduler_delete(g_scheduler);
    g_scheduler = NULL;
  }
}

envoy_dynamic_module_type_bootstrap_extension_module_ptr
envoy_dynamic_module_on_bootstrap_extension_new(
    envoy_dynamic_module_type_bootstrap_extension_config_module_ptr extension_config_ptr,
    envoy_dynamic_module_type_bootstrap_extension_envoy_ptr extension_envoy_ptr) {
  (void)extension_config_ptr;
  (void)extension_envoy_ptr;
  return (envoy_dynamic_module_type_bootstrap_extension_module_ptr)0x2;
}

void envoy_dynamic_module_on_bootstrap_extension_server_initialized(
    envoy_dynamic_module_type_bootstrap_extension_envoy_ptr extension_envoy_ptr,
    envoy_dynamic_module_type_bootstrap_extension_module_ptr extension_module_ptr) {
  (void)extension_envoy_ptr;
  (void)extension_module_ptr;
  if (g_run_on_all_workers_at_startup) {
    envoy_dynamic_module_callback_bootstrap_extension_run_on_all_workers(g_config_envoy_ptr, 1);
    envoy_dynamic_module_callback_bootstrap_extension_config_scheduler_commit(g_scheduler, 1);
  }
}

void envoy_dynamic_module_on_bootstrap_extension_worker_thread_initialized(
    envoy_dynamic_module_type_bootstrap_extension_envoy_ptr extension_envoy_ptr,
    envoy_dynamic_module_type_bootstrap_extension_module_ptr extension_module_ptr) {
  (void)extension_envoy_ptr;
  (void)extension_module_ptr;
}

void envoy_dynamic_module_on_bootstrap_extension_drain_started(
    envoy_dynamic_module_type_bootstrap_extension_envoy_ptr extension_envoy_ptr,
    envoy_dynamic_module_type_bootstrap_extension_module_ptr extension_module_ptr) {
  (void)extension_envoy_ptr;
  (void)extension_module_ptr;
}

void envoy_dynamic_module_on_bootstrap_extension_shutdown(
    envoy_dynamic_module_type_bootstrap_extension_envoy_ptr extension_envoy_ptr,
    envoy_dynamic_module_type_bootstrap_extension_module_ptr extension_module_ptr,
    envoy_dynamic_module_type_event_cb completion_callback, void* completion_context) {
  (void)extension_envoy_ptr;
  (void)extension_module_ptr;
  completion_callback(completion_context);
}

void envoy_dynamic_module_on_bootstrap_extension_destroy(
    envoy_dynamic_module_type_bootstrap_extension_module_ptr extension_module_ptr) {
  (void)extension_module_ptr;
}

void envoy_dynamic_module_on_bootstrap_extension_config_scheduled(
    envoy_dynamic_module_type_bootstrap_extension_config_envoy_ptr extension_config_envoy_ptr,
    envoy_dynamic_module_type_bootstrap_extension_config_module_ptr extension_config_ptr,
    uint64_t event_id) {
  (void)extension_config_ptr;
  (void)event_id;
  if (g_run_on_all_workers_at_startup) {
    envoy_dynamic_module_callback_bootstrap_extension_config_signal_init_complete(
        extension_config_envoy_ptr);
  }
}

void envoy_dynamic_module_on_bootstrap_extension_http_callout_done(
    envoy_dynamic_module_type_bootstrap_extension_config_envoy_ptr extension_config_envoy_ptr,
    envoy_dynamic_module_type_bootstrap_extension_config_module_ptr extension_config_module_ptr,
    uint64_t callout_id, envoy_dynamic_module_type_http_callout_result result,
    envoy_dynamic_module_type_envoy_http_header* headers, size_t headers_size,
    envoy_dynamic_module_type_envoy_buffer* body_chunks, size_t body_chunks_size) {
  (void)extension_config_envoy_ptr;
  (void)extension_config_module_ptr;
  (void)callout_id;
  (void)headers;
  (void)headers_size;
  (void)body_chunks;
  (void)body_chunks_size;
  g_http_callout_done_count++;
  g_last_http_callout_result = (int)result;
}

void envoy_dynamic_module_on_bootstrap_extension_timer_fired(
    envoy_dynamic_module_type_bootstrap_extension_config_envoy_ptr extension_config_envoy_ptr,
    envoy_dynamic_module_type_bootstrap_extension_config_module_ptr extension_config_module_ptr,
    envoy_dynamic_module_type_bootstrap_extension_timer_module_ptr timer_ptr) {
  (void)extension_config_envoy_ptr;
  (void)extension_config_module_ptr;
  (void)timer_ptr;
}

void envoy_dynamic_module_on_bootstrap_extension_file_changed(
    envoy_dynamic_module_type_bootstrap_extension_config_envoy_ptr extension_config_envoy_ptr,
    envoy_dynamic_module_type_bootstrap_extension_config_module_ptr extension_config_module_ptr,
    envoy_dynamic_module_type_envoy_buffer path, uint32_t events) {
  (void)extension_config_envoy_ptr;
  (void)extension_config_module_ptr;
  (void)path;
  (void)events;
}

uint32_t envoy_dynamic_module_on_bootstrap_extension_admin_request(
    envoy_dynamic_module_type_bootstrap_extension_config_envoy_ptr extension_config_envoy_ptr,
    envoy_dynamic_module_type_bootstrap_extension_config_module_ptr extension_config_module_ptr,
    envoy_dynamic_module_type_envoy_buffer method, envoy_dynamic_module_type_envoy_buffer path,
    envoy_dynamic_module_type_envoy_buffer body) {
  (void)extension_config_envoy_ptr;
  (void)extension_config_module_ptr;
  (void)method;
  (void)path;
  (void)body;
  return 200;
}

void envoy_dynamic_module_on_bootstrap_extension_cluster_add_or_update(
    envoy_dynamic_module_type_bootstrap_extension_config_envoy_ptr extension_config_envoy_ptr,
    envoy_dynamic_module_type_bootstrap_extension_config_module_ptr extension_config_module_ptr,
    envoy_dynamic_module_type_envoy_buffer cluster_name) {
  (void)extension_config_envoy_ptr;
  (void)extension_config_module_ptr;
  (void)cluster_name;
}

void envoy_dynamic_module_on_bootstrap_extension_cluster_removal(
    envoy_dynamic_module_type_bootstrap_extension_config_envoy_ptr extension_config_envoy_ptr,
    envoy_dynamic_module_type_bootstrap_extension_config_module_ptr extension_config_module_ptr,
    envoy_dynamic_module_type_envoy_buffer cluster_name) {
  (void)extension_config_envoy_ptr;
  (void)extension_config_module_ptr;
  (void)cluster_name;
}

void envoy_dynamic_module_on_bootstrap_extension_listener_add_or_update(
    envoy_dynamic_module_type_bootstrap_extension_config_envoy_ptr extension_config_envoy_ptr,
    envoy_dynamic_module_type_bootstrap_extension_config_module_ptr extension_config_module_ptr,
    envoy_dynamic_module_type_envoy_buffer listener_name) {
  (void)extension_config_envoy_ptr;
  (void)extension_config_module_ptr;
  (void)listener_name;
}

void envoy_dynamic_module_on_bootstrap_extension_listener_removal(
    envoy_dynamic_module_type_bootstrap_extension_config_envoy_ptr extension_config_envoy_ptr,
    envoy_dynamic_module_type_bootstrap_extension_config_module_ptr extension_config_module_ptr,
    envoy_dynamic_module_type_envoy_buffer listener_name) {
  (void)extension_config_envoy_ptr;
  (void)extension_config_module_ptr;
  (void)listener_name;
}

void envoy_dynamic_module_on_bootstrap_extension_worker_event(
    envoy_dynamic_module_type_bootstrap_extension_config_envoy_ptr extension_config_envoy_ptr,
    envoy_dynamic_module_type_bootstrap_extension_config_module_ptr extension_config_module_ptr,
    uint64_t event_id) {
  (void)extension_config_module_ptr;
  atomic_fetch_add(&g_worker_event_count, 1);
  atomic_store(&g_last_worker_event_id, event_id);
  if (g_run_on_all_workers_at_startup) {
    envoy_dynamic_module_callback_bootstrap_extension_config_increment_counter(
        extension_config_envoy_ptr, g_worker_events_counter_id, NULL, 0, 1);
  }
}

void envoy_dynamic_module_on_bootstrap_extension_run_on_all_workers_done(
    envoy_dynamic_module_type_bootstrap_extension_config_envoy_ptr extension_config_envoy_ptr,
    envoy_dynamic_module_type_bootstrap_extension_config_module_ptr extension_config_module_ptr,
    uint64_t event_id) {
  (void)extension_config_envoy_ptr;
  (void)extension_config_module_ptr;
  g_done_count++;
  g_last_done_event_id = event_id;
}

void envoy_dynamic_module_on_bootstrap_extension_cluster_writer_update_done(
    envoy_dynamic_module_type_bootstrap_extension_config_envoy_ptr extension_config_envoy_ptr,
    envoy_dynamic_module_type_bootstrap_extension_config_module_ptr extension_config_module_ptr,
    uint64_t update_id, envoy_dynamic_module_type_envoy_buffer cluster_name,
    envoy_dynamic_module_type_bootstrap_cluster_writer_update_status status,
    envoy_dynamic_module_type_envoy_buffer error_details) {
  (void)extension_config_envoy_ptr;
  (void)extension_config_module_ptr;
  g_update_done_count++;
  g_last_update_id = update_id;
  g_last_update_status = (int)status;
  copyBuffer(g_last_update_cluster_name, sizeof(g_last_update_cluster_name), cluster_name);
  copyBuffer(g_last_update_error_details, sizeof(g_last_update_error_details), error_details);
}

void envoy_dynamic_module_on_bootstrap_extension_listener_writer_update_done(
    envoy_dynamic_module_type_bootstrap_extension_config_envoy_ptr extension_config_envoy_ptr,
    envoy_dynamic_module_type_bootstrap_extension_config_module_ptr extension_config_module_ptr,
    uint64_t update_id, envoy_dynamic_module_type_envoy_buffer listener_name,
    envoy_dynamic_module_type_bootstrap_listener_writer_update_status status,
    envoy_dynamic_module_type_envoy_buffer error_details) {
  (void)extension_config_envoy_ptr;
  (void)extension_config_module_ptr;
  g_listener_update_done_count++;
  g_last_listener_update_id = update_id;
  g_last_listener_update_status = (int)status;
  copyBuffer(g_last_listener_update_name, sizeof(g_last_listener_update_name), listener_name);
  copyBuffer(g_last_listener_update_error_details, sizeof(g_last_listener_update_error_details),
             error_details);
}

size_t bootstrap_optional_hooks_test_worker_event_count(void) {
  return atomic_load(&g_worker_event_count);
}

uint64_t bootstrap_optional_hooks_test_last_worker_event_id(void) {
  return atomic_load(&g_last_worker_event_id);
}

size_t bootstrap_optional_hooks_test_done_count(void) { return g_done_count; }

uint64_t bootstrap_optional_hooks_test_last_done_event_id(void) { return g_last_done_event_id; }

size_t bootstrap_optional_hooks_test_update_done_count(void) { return g_update_done_count; }

uint64_t bootstrap_optional_hooks_test_last_update_id(void) { return g_last_update_id; }

int bootstrap_optional_hooks_test_last_update_status(void) { return g_last_update_status; }

const char* bootstrap_optional_hooks_test_last_update_cluster_name(void) {
  return g_last_update_cluster_name;
}

const char* bootstrap_optional_hooks_test_last_update_error_details(void) {
  return g_last_update_error_details;
}

size_t bootstrap_optional_hooks_test_listener_update_done_count(void) {
  return g_listener_update_done_count;
}

uint64_t bootstrap_optional_hooks_test_last_listener_update_id(void) {
  return g_last_listener_update_id;
}

int bootstrap_optional_hooks_test_last_listener_update_status(void) {
  return g_last_listener_update_status;
}

const char* bootstrap_optional_hooks_test_last_listener_update_name(void) {
  return g_last_listener_update_name;
}

const char* bootstrap_optional_hooks_test_last_listener_update_error_details(void) {
  return g_last_listener_update_error_details;
}

size_t bootstrap_optional_hooks_test_http_callout_done_count(void) {
  return g_http_callout_done_count;
}

int bootstrap_optional_hooks_test_last_http_callout_result(void) {
  return g_last_http_callout_result;
}
