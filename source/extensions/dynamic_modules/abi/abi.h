#pragma once

// NOLINT(namespace-envoy)

// This is a pure C header, so we can't apply clang-tidy to it.
// NOLINTBEGIN

// This is a pure C header file that defines the ABI of the core of dynamic modules used by Envoy.
//
// This must not contain any dependencies besides standard library since it is not only used by
// Envoy itself but also by dynamic module SDKs written in non-C++ languages.
//
// There are three kinds defined in this file:
//
//  * Types: type definitions used in the ABI.
//  * Events Hooks: functions that modules must implement to handle events from Envoy.
//  * Callbacks: functions that Envoy implements and modules can call to interact with Envoy.
//
// Types are prefixed with "envoy_dynamic_module_type_". Event Hooks are prefixed with
// "envoy_dynamic_module_on_". Callbacks are prefixed with "envoy_dynamic_module_callback_".
//
// Some functions are specified/defined under the assumptions that all dynamic modules are trusted
// and have the same privilege level as the main Envoy program. This is because they run inside the
// Envoy process, hence they can access all the memory and resources that the main Envoy process
// can, which makes it impossible to enforce any security boundaries between Envoy and the modules
// by nature. For example, we assume that modules will not try to pass invalid pointers to Envoy
// intentionally.

// This is the ABI version that we bump the minor version at least once for any ABI changes in same
// Envoy release cycle to indicate the ABI change.
//
// Break change in the ABI is not allowed except the ABI has not been released yet.
//
// Until we reach v1.0, we only guarantee backward
// compatibility in the next minor version. For example, v0.1.y is guaranteed to be compatible with
// v0.2.x, but not with v0.3.x.
//
// This is used only for tracking the ABI version of dynamic modules and emitting warnings when
// there's a mismatch.
//
// Note(internal): We could use the Envoy's version such as "v1.38.0" here, there are several
// reasons as to why we use a static version string instead:
// 1. Envoy's version is generated at the build time of Envoy while we need to make it available for
// SDK downstream users.
// 2. In the future, after the stable ABI is established, we may want to decouple the ABI version
// from Envoy's versioning scheme.
#define ENVOY_DYNAMIC_MODULES_ABI_VERSION "v0.1.0"

#ifdef __cplusplus
#include <cstdbool>
#include <cstddef>
#include <cstdint>

constexpr const char* envoy_dynamic_modules_abi_version = ENVOY_DYNAMIC_MODULES_ABI_VERSION;

extern "C" {
#else
const char* __attribute__((weak)) envoy_dynamic_modules_abi_version =
    ENVOY_DYNAMIC_MODULES_ABI_VERSION;

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#endif

// =============================================================================
// ==================================== Common =================================
// =============================================================================

// =============================================================================
// Common Types
// =============================================================================

/**
 * envoy_dynamic_module_type_abi_version_module_ptr represents a null-terminated string that
 * contains the ABI version of the dynamic module. This is used to ensure that the dynamic module is
 * built against the compatible version of the ABI.
 *
 * OWNERSHIP: Module owns the pointer. The string must remain valid until the end of
 * envoy_dynamic_module_on_program_init function.
 */
typedef const char* envoy_dynamic_module_type_abi_version_module_ptr;

/**
 * envoy_dynamic_module_type_buffer_module_ptr is a pointer to a buffer in the module. A buffer
 * represents a contiguous block of memory in bytes.
 *
 * OWNERSHIP: The module is responsible for managing the lifetime of the pointer. It depends on the
 * context where the buffer is used. See for the specific event hook or callback for more details.
 */
typedef const char* envoy_dynamic_module_type_buffer_module_ptr;

/**
 * envoy_dynamic_module_type_buffer_envoy_ptr is a pointer to a buffer in Envoy. A buffer represents
 * a contiguous block of memory in bytes.
 *
 * OWNERSHIP: Envoy owns the pointer. The lifetime depends on the context where the buffer is used.
 * See for the specific event hook or callback for more details.
 */
typedef const char* envoy_dynamic_module_type_buffer_envoy_ptr;

/**
 * envoy_dynamic_module_type_envoy_buffer represents a buffer owned by Envoy.
 * This is to give the direct access to the buffer in Envoy.
 */
typedef struct envoy_dynamic_module_type_envoy_buffer {
  envoy_dynamic_module_type_buffer_envoy_ptr ptr;
  size_t length;
} envoy_dynamic_module_type_envoy_buffer;

/**
 * envoy_dynamic_module_type_module_buffer represents a buffer owned by the module.
 */
typedef struct envoy_dynamic_module_type_module_buffer {
  envoy_dynamic_module_type_buffer_module_ptr ptr;
  size_t length;
} envoy_dynamic_module_type_module_buffer;

/**
 * envoy_dynamic_module_type_module_http_header represents a key-value pair of an HTTP header owned
 * by the module.
 */
typedef struct envoy_dynamic_module_type_module_http_header {
  envoy_dynamic_module_type_buffer_module_ptr key_ptr;
  size_t key_length;
  envoy_dynamic_module_type_buffer_module_ptr value_ptr;
  size_t value_length;
} envoy_dynamic_module_type_module_http_header;

/**
 * envoy_dynamic_module_type_envoy_http_header represents a key-value pair of an HTTP header owned
 * by Envoy's HeaderMap.
 */
typedef struct envoy_dynamic_module_type_envoy_http_header {
  envoy_dynamic_module_type_buffer_envoy_ptr key_ptr;
  size_t key_length;
  envoy_dynamic_module_type_buffer_envoy_ptr value_ptr;
  size_t value_length;
} envoy_dynamic_module_type_envoy_http_header;

typedef enum envoy_dynamic_module_type_http_header_type {
  envoy_dynamic_module_type_http_header_type_RequestHeader,
  envoy_dynamic_module_type_http_header_type_RequestTrailer,
  envoy_dynamic_module_type_http_header_type_ResponseHeader,
  envoy_dynamic_module_type_http_header_type_ResponseTrailer,
} envoy_dynamic_module_type_http_header_type;

/**
 * envoy_dynamic_module_type_log_level represents the log level passed to
 * envoy_dynamic_module_callback_log. This corresponds to the enum defined in
 * source/common/common/base_logger.h.
 */
typedef enum envoy_dynamic_module_type_log_level {
  envoy_dynamic_module_type_log_level_Trace,
  envoy_dynamic_module_type_log_level_Debug,
  envoy_dynamic_module_type_log_level_Info,
  envoy_dynamic_module_type_log_level_Warn,
  envoy_dynamic_module_type_log_level_Error,
  envoy_dynamic_module_type_log_level_Critical,
  envoy_dynamic_module_type_log_level_Off,
} envoy_dynamic_module_type_log_level;

/**
 * envoy_dynamic_module_type_http_callout_init_result represents the result of the HTTP callout
 * initialization after envoy_dynamic_module_callback_http_filter_http_callout is called.
 * Success means the callout is successfully initialized and ready to be used.
 * MissingRequiredHeaders means the callout is missing one of the required headers, :path, :method,
 * or host header. DuplicateCalloutId means the callout id is already used by another callout.
 * ClusterNotFound means the cluster is not found in the configuration. CannotCreateRequest means
 * the request cannot be created. That happens when, for example, there's no healthy upstream host
 * in the cluster.
 */
typedef enum envoy_dynamic_module_type_http_callout_init_result {
  envoy_dynamic_module_type_http_callout_init_result_Success,
  envoy_dynamic_module_type_http_callout_init_result_MissingRequiredHeaders,
  envoy_dynamic_module_type_http_callout_init_result_ClusterNotFound,
  envoy_dynamic_module_type_http_callout_init_result_DuplicateCalloutId,
  envoy_dynamic_module_type_http_callout_init_result_CannotCreateRequest,
} envoy_dynamic_module_type_http_callout_init_result;

/**
 * envoy_dynamic_module_type_http_callout_result represents the result of the HTTP callout.
 * This corresponds to `AsyncClient::FailureReason::*` in envoy/http/async_client.h plus Success.
 */
typedef enum envoy_dynamic_module_type_http_callout_result {
  envoy_dynamic_module_type_http_callout_result_Success,
  envoy_dynamic_module_type_http_callout_result_Reset,
  envoy_dynamic_module_type_http_callout_result_ExceedResponseBufferLimit,
} envoy_dynamic_module_type_http_callout_result;

/**
 * envoy_dynamic_module_type_http_stream_reset_reason represents the reason for a stream reset.
 * This corresponds to `AsyncClient::StreamResetReason::*` in envoy/http/async_client.h.
 */
typedef enum envoy_dynamic_module_type_http_stream_reset_reason {
  envoy_dynamic_module_type_http_stream_reset_reason_ConnectionFailure,
  envoy_dynamic_module_type_http_stream_reset_reason_ConnectionTermination,
  envoy_dynamic_module_type_http_stream_reset_reason_LocalReset,
  envoy_dynamic_module_type_http_stream_reset_reason_LocalRefusedStreamReset,
  envoy_dynamic_module_type_http_stream_reset_reason_Overflow,
  envoy_dynamic_module_type_http_stream_reset_reason_RemoteReset,
  envoy_dynamic_module_type_http_stream_reset_reason_RemoteRefusedStreamReset,
  envoy_dynamic_module_type_http_stream_reset_reason_ProtocolError,
} envoy_dynamic_module_type_http_stream_reset_reason;

/**
 * envoy_dynamic_module_type_metrics_result represents the result of the metrics operation.
 * Success means the operation was successful.
 * MetricNotFound means the metric was not found. This is usually an indication that a handle was
 * improperly initialized or stored. InvalidLabels means the labels are invalid. Frozen means a
 * metric was attempted to be created when the stats creation is frozen.
 */
typedef enum envoy_dynamic_module_type_metrics_result {
  envoy_dynamic_module_type_metrics_result_Success,
  envoy_dynamic_module_type_metrics_result_MetricNotFound,
  envoy_dynamic_module_type_metrics_result_InvalidLabels,
  envoy_dynamic_module_type_metrics_result_Frozen,
} envoy_dynamic_module_type_metrics_result;

/**
 * envoy_dynamic_module_type_metadata_source represents the location of metadata to get when calling
 * envoy_dynamic_module_callback_http_get_metadata_* functions.
 */
typedef enum envoy_dynamic_module_type_metadata_source {
  // stream's dynamic metadata.
  envoy_dynamic_module_type_metadata_source_Dynamic,
  // route metadata
  envoy_dynamic_module_type_metadata_source_Route,
  // cluster metadata
  envoy_dynamic_module_type_metadata_source_Cluster,
  // host (LbEndpoint in xDS) metadata
  envoy_dynamic_module_type_metadata_source_Host,
  // host locality (LocalityLbEndpoints in xDS) metadata
  envoy_dynamic_module_type_metadata_source_HostLocality,
} envoy_dynamic_module_type_metadata_source;

/**
 * envoy_dynamic_module_type_attribute_id represents an attribute described in
 * https://www.envoyproxy.io/docs/envoy/latest/intro/arch_overview/advanced/attributes
 */
typedef enum envoy_dynamic_module_type_attribute_id {
  // request.path
  envoy_dynamic_module_type_attribute_id_RequestPath,
  // request.url_path
  envoy_dynamic_module_type_attribute_id_RequestUrlPath,
  // request.host
  envoy_dynamic_module_type_attribute_id_RequestHost,
  // request.scheme
  envoy_dynamic_module_type_attribute_id_RequestScheme,
  // request.method
  envoy_dynamic_module_type_attribute_id_RequestMethod,
  // request.headers
  envoy_dynamic_module_type_attribute_id_RequestHeaders,
  // request.referer
  envoy_dynamic_module_type_attribute_id_RequestReferer,
  // request.useragent
  envoy_dynamic_module_type_attribute_id_RequestUserAgent,
  // request.time
  envoy_dynamic_module_type_attribute_id_RequestTime,
  // request.id
  envoy_dynamic_module_type_attribute_id_RequestId,
  // request.protocol
  envoy_dynamic_module_type_attribute_id_RequestProtocol,
  // request.query
  envoy_dynamic_module_type_attribute_id_RequestQuery,
  // request.duration
  envoy_dynamic_module_type_attribute_id_RequestDuration,
  // request.size
  envoy_dynamic_module_type_attribute_id_RequestSize,
  // request.total_size
  envoy_dynamic_module_type_attribute_id_RequestTotalSize,
  // response.code
  envoy_dynamic_module_type_attribute_id_ResponseCode,
  // response.code_details
  envoy_dynamic_module_type_attribute_id_ResponseCodeDetails,
  // response.flags
  envoy_dynamic_module_type_attribute_id_ResponseFlags,
  // response.grpc_status
  envoy_dynamic_module_type_attribute_id_ResponseGrpcStatus,
  // response.headers
  envoy_dynamic_module_type_attribute_id_ResponseHeaders,
  // response.trailers
  envoy_dynamic_module_type_attribute_id_ResponseTrailers,
  // response.size
  envoy_dynamic_module_type_attribute_id_ResponseSize,
  // response.total_size
  envoy_dynamic_module_type_attribute_id_ResponseTotalSize,
  // response.backend_latency
  envoy_dynamic_module_type_attribute_id_ResponseBackendLatency,
  // source.address
  envoy_dynamic_module_type_attribute_id_SourceAddress,
  // source.port
  envoy_dynamic_module_type_attribute_id_SourcePort,
  // destination.address
  envoy_dynamic_module_type_attribute_id_DestinationAddress,
  // destination.port
  envoy_dynamic_module_type_attribute_id_DestinationPort,
  // connection.id
  envoy_dynamic_module_type_attribute_id_ConnectionId,
  // connection.mtls
  envoy_dynamic_module_type_attribute_id_ConnectionMtls,
  // connection.requested_server_name
  envoy_dynamic_module_type_attribute_id_ConnectionRequestedServerName,
  // connection.tls_version
  envoy_dynamic_module_type_attribute_id_ConnectionTlsVersion,
  // connection.subject_local_certificate
  envoy_dynamic_module_type_attribute_id_ConnectionSubjectLocalCertificate,
  // connection.subject_peer_certificate
  envoy_dynamic_module_type_attribute_id_ConnectionSubjectPeerCertificate,
  // connection.dns_san_local_certificate
  envoy_dynamic_module_type_attribute_id_ConnectionDnsSanLocalCertificate,
  // connection.dns_san_peer_certificate
  envoy_dynamic_module_type_attribute_id_ConnectionDnsSanPeerCertificate,
  // connection.uri_san_local_certificate
  envoy_dynamic_module_type_attribute_id_ConnectionUriSanLocalCertificate,
  // connection.uri_san_peer_certificate
  envoy_dynamic_module_type_attribute_id_ConnectionUriSanPeerCertificate,
  // connection.sha256_peer_certificate_digest
  envoy_dynamic_module_type_attribute_id_ConnectionSha256PeerCertificateDigest,
  // connection.transport_failure_reason
  envoy_dynamic_module_type_attribute_id_ConnectionTransportFailureReason,
  // connection.termination_details
  envoy_dynamic_module_type_attribute_id_ConnectionTerminationDetails,
  // upstream.address
  envoy_dynamic_module_type_attribute_id_UpstreamAddress,
  // upstream.port
  envoy_dynamic_module_type_attribute_id_UpstreamPort,
  // upstream.tls_version
  envoy_dynamic_module_type_attribute_id_UpstreamTlsVersion,
  // upstream.subject_local_certificate
  envoy_dynamic_module_type_attribute_id_UpstreamSubjectLocalCertificate,
  // upstream.subject_peer_certificate
  envoy_dynamic_module_type_attribute_id_UpstreamSubjectPeerCertificate,
  // upstream.dns_san_local_certificate
  envoy_dynamic_module_type_attribute_id_UpstreamDnsSanLocalCertificate,
  // upstream.dns_san_peer_certificate
  envoy_dynamic_module_type_attribute_id_UpstreamDnsSanPeerCertificate,
  // upstream.uri_san_local_certificate
  envoy_dynamic_module_type_attribute_id_UpstreamUriSanLocalCertificate,
  // upstream.uri_san_peer_certificate
  envoy_dynamic_module_type_attribute_id_UpstreamUriSanPeerCertificate,
  // upstream.sha256_peer_certificate_digest
  envoy_dynamic_module_type_attribute_id_UpstreamSha256PeerCertificateDigest,
  // upstream.local_address
  envoy_dynamic_module_type_attribute_id_UpstreamLocalAddress,
  // upstream.transport_failure_reason
  envoy_dynamic_module_type_attribute_id_UpstreamTransportFailureReason,
  // upstream.request_attempt_count
  envoy_dynamic_module_type_attribute_id_UpstreamRequestAttemptCount,
  // upstream.cx_pool_ready_duration
  envoy_dynamic_module_type_attribute_id_UpstreamCxPoolReadyDuration,
  // upstream.locality
  envoy_dynamic_module_type_attribute_id_UpstreamLocality,
  // xds.node
  envoy_dynamic_module_type_attribute_id_XdsNode,
  // xds.cluster_name
  envoy_dynamic_module_type_attribute_id_XdsClusterName,
  // xds.cluster_metadata
  envoy_dynamic_module_type_attribute_id_XdsClusterMetadata,
  // xds.listener_direction
  envoy_dynamic_module_type_attribute_id_XdsListenerDirection,
  // xds.listener_metadata
  envoy_dynamic_module_type_attribute_id_XdsListenerMetadata,
  // xds.route_name
  envoy_dynamic_module_type_attribute_id_XdsRouteName,
  // xds.route_metadata
  envoy_dynamic_module_type_attribute_id_XdsRouteMetadata,
  // xds.virtual_host_name
  envoy_dynamic_module_type_attribute_id_XdsVirtualHostName,
  // xds.virtual_host_metadata
  envoy_dynamic_module_type_attribute_id_XdsVirtualHostMetadata,
  // xds.upstream_host_metadata
  envoy_dynamic_module_type_attribute_id_XdsUpstreamHostMetadata,
  // xds.filter_chain_name
  envoy_dynamic_module_type_attribute_id_XdsFilterChainName,
  // health_check
  envoy_dynamic_module_type_attribute_id_HealthCheck,
} envoy_dynamic_module_type_attribute_id;

/**
 * envoy_dynamic_module_type_socket_option_state represents the socket state at which an option
 * should be applied.
 */
typedef enum envoy_dynamic_module_type_socket_option_state {
  envoy_dynamic_module_type_socket_option_state_Prebind = 0,
  envoy_dynamic_module_type_socket_option_state_Bound = 1,
  envoy_dynamic_module_type_socket_option_state_Listening = 2,
} envoy_dynamic_module_type_socket_option_state;

/**
 * envoy_dynamic_module_type_socket_option_value_type represents the type of value stored in a
 * socket option.
 */
typedef enum envoy_dynamic_module_type_socket_option_value_type {
  envoy_dynamic_module_type_socket_option_value_type_Int = 0,
  envoy_dynamic_module_type_socket_option_value_type_Bytes = 1,
} envoy_dynamic_module_type_socket_option_value_type;

/**
 * envoy_dynamic_module_type_socket_option represents a socket option with its level, name, state,
 * and value. The value can be either an integer or bytes depending on value_type.
 */
typedef struct envoy_dynamic_module_type_socket_option {
  int64_t level;
  int64_t name;
  envoy_dynamic_module_type_socket_option_state state;
  envoy_dynamic_module_type_socket_option_value_type value_type;
  int64_t int_value;
  envoy_dynamic_module_type_envoy_buffer byte_value;
} envoy_dynamic_module_type_socket_option;

/**
 * envoy_dynamic_module_type_address_type represents the socket address type.
 */
typedef enum envoy_dynamic_module_type_address_type {
  envoy_dynamic_module_type_address_type_Unknown = 0,
  envoy_dynamic_module_type_address_type_Ip = 1,
  envoy_dynamic_module_type_address_type_Pipe = 2,
  envoy_dynamic_module_type_address_type_EnvoyInternal = 3,
} envoy_dynamic_module_type_address_type;

/**
 * envoy_dynamic_module_type_socket_direction represents whether the socket option should be
 * applied to the upstream (outgoing to backend) or downstream (incoming from client) connection.
 */
typedef enum envoy_dynamic_module_type_socket_direction {
  envoy_dynamic_module_type_socket_direction_Upstream = 0,
  envoy_dynamic_module_type_socket_direction_Downstream = 1,
} envoy_dynamic_module_type_socket_direction;

// =============================================================================
// Common Event Hooks
// =============================================================================
// Event hooks are functions that are called by Envoy in response to certain events.
// The module must implement and export these functions in the dynamic module object file.
//
// Each event hook is defined as a function prototype. The symbol must be prefixed with
// "envoy_dynamic_module_on_".

/**
 * envoy_dynamic_module_on_program_init is called by the main thread exactly when the module is
 * loaded. The function returns the ABI version of the dynamic module. If null is returned, the
 * module will be unloaded immediately.
 *
 * For Envoy, the return value will be used to check the compatibility of the dynamic module.
 *
 * For dynamic modules, this is useful when they need to perform some process-wide
 * initialization or check if the module is compatible with the platform, such as CPU features.
 * Note that initialization routines of a dynamic module can also be performed without this function
 * through constructor functions in an object file. However, normal constructors cannot be used
 * to check compatibility and gracefully fail the initialization because there is no way to
 * report an error to Envoy.
 *
 * @return envoy_dynamic_module_type_abi_version_module_ptr is the ABI version of the dynamic
 * module. Null means the error and the module will be unloaded immediately.
 */
envoy_dynamic_module_type_abi_version_module_ptr envoy_dynamic_module_on_program_init(void);

// =============================================================================
// Common Callbacks
// =============================================================================

// --------------------------------- Logging -----------------------------------

/**
 * envoy_dynamic_module_callback_log is called by the module to log a message as part
 * of the standard Envoy logging stream under [dynamic_modules] Id.
 *
 * @param level is the log level of the message.
 * @param message is the log message to be logged.
 *
 */
void envoy_dynamic_module_callback_log(envoy_dynamic_module_type_log_level level,
                                       envoy_dynamic_module_type_module_buffer message);

/**
 * envoy_dynamic_module_callback_log_enabled is called by the module to check if the log level is
 * enabled for logging for the dynamic modules Id. This can be used to avoid unnecessary
 * string formatting and allocation if the log level is not enabled since calling this function
 * should be negligible in terms of performance.
 *
 * @param level is the log level to check.
 * @return true if the log level is enabled, false otherwise.
 */
bool envoy_dynamic_module_callback_log_enabled(envoy_dynamic_module_type_log_level level);

// --------------------------------- Threading -----------------------------------

/**
 * envoy_dynamic_module_callback_get_concurrency may be called by the dynamic
 * module in envoy_dynamic_module_on_program_init to get the configured concurrency of the server.
 * NOTE: This function must be called on the main thread.
 *
 * @return number of worker threads (concurrency) that the server is configured to use.
 */
uint32_t envoy_dynamic_module_callback_get_concurrency();

// ----------------------------- Server Mode -----------------------------------

/**
 * envoy_dynamic_module_callback_is_validation_mode may be called by the dynamic
 * module to check if the server is running in config validation mode (--mode validate).
 * This allows modules to optimize by only parsing and validating their config without
 * performing expensive operations such as provider lookups or loading external resources.
 * NOTE: This function must be called on the main thread.
 *
 * @return true if the server is in validation mode, false otherwise.
 */
bool envoy_dynamic_module_callback_is_validation_mode();

// ----------------------------- Function Registry -----------------------------

/**
 * envoy_dynamic_module_callback_register_function registers an opaque function pointer under the
 * given key in the process-wide function registry. This allows modules loaded in the same process
 * to expose functions that other modules can resolve by name and call directly, enabling zero-copy
 * cross-module interactions.
 *
 * Registration is typically done once during bootstrap (e.g., in on_server_initialized). The
 * function pointer must remain valid for the lifetime of the process.
 *
 * Callers are responsible for agreeing on the function signature out-of-band, since the registry
 * stores opaque void* pointers — analogous to dlsym semantics.
 *
 * This is thread-safe and can be called from any thread.
 *
 * @param key is the name to register the function under.
 * @param function_ptr is the function pointer to register. Must not be nullptr.
 * @return true if registered successfully, false if a function is already registered under key or
 *         function_ptr is nullptr.
 */
bool envoy_dynamic_module_callback_register_function(envoy_dynamic_module_type_module_buffer key,
                                                     void* function_ptr);

/**
 * envoy_dynamic_module_callback_get_function retrieves a previously registered function pointer by
 * key from the process-wide function registry. The returned pointer can be cast to the expected
 * function signature and called directly.
 *
 * Resolution is typically done once during configuration creation (e.g., in
 * on_http_filter_config_new) and the result cached for per-request use.
 *
 * This is thread-safe and can be called from any thread.
 *
 * @param key is the name of the function to retrieve.
 * @param function_ptr_out is a pointer to a variable where the function pointer will be stored.
 *                         This is only written to if the function returns true.
 * @return true if a function was found under the given key, false otherwise.
 */
bool envoy_dynamic_module_callback_get_function(envoy_dynamic_module_type_module_buffer key,
                                                void** function_ptr_out);

// ----------------------------- Shared Data Registry -----------------------------

/**
 * envoy_dynamic_module_callback_register_shared_data registers an opaque data pointer under the
 * given key in the process-wide shared data registry. This allows modules loaded in the same
 * process to share arbitrary state — such as runtime handles, configuration snapshots, or shared
 * caches — without requiring direct access to each other's globals.
 *
 * Unlike the function registry, the shared data registry allows overwriting an existing entry.
 * If the key already exists, the data pointer is updated and the function returns true. This
 * supports patterns where shared state is refreshed (e.g., after a configuration reload).
 * Callers are responsible for managing the lifetime of overwritten data pointers.
 *
 * Registration is typically done once during bootstrap (e.g., in on_server_initialized or
 * on_scheduled). The data pointer must remain valid for the lifetime of the process.
 *
 * This is thread-safe and can be called from any thread.
 *
 * @param key is the name to register the data under.
 * @param data_ptr is the data pointer to register. Must not be nullptr.
 * @return true if registered or updated successfully, false if data_ptr is nullptr.
 */
bool envoy_dynamic_module_callback_register_shared_data(envoy_dynamic_module_type_module_buffer key,
                                                        void* data_ptr);

/**
 * envoy_dynamic_module_callback_get_shared_data retrieves a previously registered data pointer by
 * key from the process-wide shared data registry. The returned pointer can be cast to the expected
 * data type and used directly.
 *
 * Resolution is typically done once during configuration creation (e.g., in
 * on_http_filter_config_new) and the result cached for per-request use.
 *
 * This is thread-safe and can be called from any thread.
 *
 * @param key is the name of the data to retrieve.
 * @param data_ptr_out is a pointer to a variable where the data pointer will be stored.
 *                     This is only written to if the function returns true.
 * @return true if data was found under the given key, false otherwise.
 */
bool envoy_dynamic_module_callback_get_shared_data(envoy_dynamic_module_type_module_buffer key,
                                                   void** data_ptr_out);

// =============================================================================
// ============================== HTTP Filter ==================================
// =============================================================================

// =============================================================================
// HTTP Filter Types
// =============================================================================

/**
 * envoy_dynamic_module_type_http_filter_config_envoy_ptr is a raw pointer to
 * the DynamicModuleHttpFilterConfig class in Envoy. This is passed to the module when
 * creating a new in-module HTTP filter configuration and used to access the HTTP filter-scoped
 * information such as metadata, metrics, etc.
 *
 * This has 1:1 correspondence with envoy_dynamic_module_type_http_filter_config_module_ptr in
 * the module.
 *
 * OWNERSHIP: Envoy owns the pointer.
 */
typedef void* envoy_dynamic_module_type_http_filter_config_envoy_ptr;

/**
 * envoy_dynamic_module_type_http_filter_config_module_ptr is a pointer to an in-module HTTP
 * configuration corresponding to an Envoy HTTP filter configuration. The config is responsible for
 * creating a new HTTP filter that corresponds to each HTTP stream.
 *
 * This has 1:1 correspondence with the DynamicModuleHttpFilterConfig class in Envoy.
 *
 * OWNERSHIP: The module is responsible for managing the lifetime of the pointer. The pointer can be
 * released when envoy_dynamic_module_on_http_filter_config_destroy is called for the same pointer.
 */
typedef const void* envoy_dynamic_module_type_http_filter_config_module_ptr;

/**
 * envoy_dynamic_module_type_http_filter_per_route_config_module_ptr is a pointer to an in-module
 * HTTP configuration corresponding to an Envoy HTTP per route filter configuration. The config is
 * responsible for changing HTTP filter's behavior on specific routes.
 *
 * This has 1:1 correspondence with the DynamicModuleHttpPerRouteFilterConfig class in Envoy.
 *
 * OWNERSHIP: The module is responsible for managing the lifetime of the pointer. The pointer can be
 * released when envoy_dynamic_module_on_http_filter_per_route_config_destroy is called for the same
 * pointer.
 */
typedef const void* envoy_dynamic_module_type_http_filter_per_route_config_module_ptr;

/**
 * envoy_dynamic_module_type_http_filter_envoy_ptr is a raw pointer to the DynamicModuleHttpFilter
 * class in Envoy. This is passed to the module when creating a new HTTP filter for each HTTP stream
 * and used to access the HTTP filter-scoped information such as headers, body, trailers, etc.
 *
 * This has 1:1 correspondence with envoy_dynamic_module_type_http_filter_module_ptr in the module.
 *
 * OWNERSHIP: Envoy owns the pointer, and can be accessed by the module until the filter is
 * destroyed, i.e. envoy_dynamic_module_on_http_filter_destroy is called.
 */
typedef void* envoy_dynamic_module_type_http_filter_envoy_ptr;

/**
 * envoy_dynamic_module_type_http_filter_module_ptr is a pointer to an in-module HTTP filter
 * corresponding to an Envoy HTTP filter. The filter is responsible for processing each HTTP stream.
 *
 * This has 1:1 correspondence with the DynamicModuleHttpFilter class in Envoy.
 *
 * OWNERSHIP: The module is responsible for managing the lifetime of the pointer. The pointer can be
 * released when envoy_dynamic_module_on_http_filter_destroy is called for the same pointer.
 */
typedef const void* envoy_dynamic_module_type_http_filter_module_ptr;

/**
 * envoy_dynamic_module_type_http_filter_scheduler_ptr is a raw pointer to the
 * DynamicModuleHttpFilterScheduler class in Envoy.
 *
 * OWNERSHIP: The allocation is done by Envoy but the module is responsible for managing the
 * lifetime of the pointer. Notably, it must be explicitly destroyed by the module
 * when scheduling the HTTP filter event is done. The creation of this pointer is done by
 * envoy_dynamic_module_callback_http_filter_scheduler_new and the scheduling and destruction is
 * done by envoy_dynamic_module_callback_http_filter_scheduler_delete. Since its lifecycle is
 * owned/managed by the module, this has _module_ptr suffix.
 */
typedef void* envoy_dynamic_module_type_http_filter_scheduler_module_ptr;

/**
 * envoy_dynamic_module_type_http_filter_config_scheduler_module_ptr is a raw pointer to the
 * DynamicModuleHttpFilterConfigScheduler class in Envoy.
 *
 * OWNERSHIP: The allocation is done by Envoy but the module is responsible for managing the
 * lifetime of the pointer. Notably, it must be explicitly destroyed by the module
 * when scheduling the HTTP filter config event is done. The creation of this pointer is done by
 * envoy_dynamic_module_callback_http_filter_config_scheduler_new and the scheduling and destruction
 * is done by envoy_dynamic_module_callback_http_filter_config_scheduler_delete. Since its lifecycle
 * is owned/managed by the module, this has _module_ptr suffix.
 */
typedef void* envoy_dynamic_module_type_http_filter_config_scheduler_module_ptr;

typedef enum envoy_dynamic_module_type_http_body_type {
  envoy_dynamic_module_type_http_body_type_ReceivedRequestBody,
  envoy_dynamic_module_type_http_body_type_BufferedRequestBody,
  envoy_dynamic_module_type_http_body_type_ReceivedResponseBody,
  envoy_dynamic_module_type_http_body_type_BufferedResponseBody,
} envoy_dynamic_module_type_http_body_type;

/**
 * envoy_dynamic_module_type_on_http_filter_request_headers_status represents the status of the
 * filter after processing the HTTP request headers. This corresponds to `FilterHeadersStatus` in
 * envoy/http/filter.h.
 */
typedef enum envoy_dynamic_module_type_on_http_filter_request_headers_status {
  envoy_dynamic_module_type_on_http_filter_request_headers_status_Continue,
  envoy_dynamic_module_type_on_http_filter_request_headers_status_StopIteration,
  envoy_dynamic_module_type_on_http_filter_request_headers_status_ContinueAndDontEndStream,
  envoy_dynamic_module_type_on_http_filter_request_headers_status_StopAllIterationAndBuffer,
  envoy_dynamic_module_type_on_http_filter_request_headers_status_StopAllIterationAndWatermark,
} envoy_dynamic_module_type_on_http_filter_request_headers_status;

/**
 * envoy_dynamic_module_type_on_http_filter_request_body_status represents the status of the filter
 * after processing the HTTP request body. This corresponds to `FilterDataStatus` in
 * envoy/http/filter.h.
 */
typedef enum envoy_dynamic_module_type_on_http_filter_request_body_status {
  envoy_dynamic_module_type_on_http_filter_request_body_status_Continue,
  envoy_dynamic_module_type_on_http_filter_request_body_status_StopIterationAndBuffer,
  envoy_dynamic_module_type_on_http_filter_request_body_status_StopIterationAndWatermark,
  envoy_dynamic_module_type_on_http_filter_request_body_status_StopIterationNoBuffer
} envoy_dynamic_module_type_on_http_filter_request_body_status;

/**
 * envoy_dynamic_module_type_on_http_filter_request_trailers_status represents the status of the
 * filter after processing the HTTP request trailers. This corresponds to `FilterTrailersStatus` in
 * envoy/http/filter.h.
 */
typedef enum envoy_dynamic_module_type_on_http_filter_request_trailers_status {
  envoy_dynamic_module_type_on_http_filter_request_trailers_status_Continue,
  envoy_dynamic_module_type_on_http_filter_request_trailers_status_StopIteration
} envoy_dynamic_module_type_on_http_filter_request_trailers_status;

/**
 * envoy_dynamic_module_type_on_http_filter_response_headers_status represents the status of the
 * filter after processing the HTTP response headers. This corresponds to `FilterHeadersStatus` in
 * envoy/http/filter.h.
 */
typedef enum envoy_dynamic_module_type_on_http_filter_response_headers_status {
  envoy_dynamic_module_type_on_http_filter_response_headers_status_Continue,
  envoy_dynamic_module_type_on_http_filter_response_headers_status_StopIteration,
  envoy_dynamic_module_type_on_http_filter_response_headers_status_ContinueAndDontEndStream,
  envoy_dynamic_module_type_on_http_filter_response_headers_status_StopAllIterationAndBuffer,
  envoy_dynamic_module_type_on_http_filter_response_headers_status_StopAllIterationAndWatermark,
} envoy_dynamic_module_type_on_http_filter_response_headers_status;

/**
 * envoy_dynamic_module_type_on_http_filter_response_body_status represents the status of the filter
 * after processing the HTTP response body. This corresponds to `FilterDataStatus` in
 * envoy/http/filter.h.
 */
typedef enum envoy_dynamic_module_type_on_http_filter_response_body_status {
  envoy_dynamic_module_type_on_http_filter_response_body_status_Continue,
  envoy_dynamic_module_type_on_http_filter_response_body_status_StopIterationAndBuffer,
  envoy_dynamic_module_type_on_http_filter_response_body_status_StopIterationAndWatermark,
  envoy_dynamic_module_type_on_http_filter_response_body_status_StopIterationNoBuffer
} envoy_dynamic_module_type_on_http_filter_response_body_status;

/**
 * envoy_dynamic_module_type_on_http_filter_response_trailers_status represents the status of the
 * filter after processing the HTTP response trailers. This corresponds to `FilterTrailersStatus` in
 * envoy/http/filter.h.
 */
typedef enum envoy_dynamic_module_type_on_http_filter_response_trailers_status {
  envoy_dynamic_module_type_on_http_filter_response_trailers_status_Continue,
  envoy_dynamic_module_type_on_http_filter_response_trailers_status_StopIteration
} envoy_dynamic_module_type_on_http_filter_response_trailers_status;

/**
 * envoy_dynamic_module_type_on_http_filter_local_reply_status represents the action to take after
 * the onLocalReply hook completes. This corresponds to `LocalErrorStatus` in envoy/http/filter.h.
 */
typedef enum envoy_dynamic_module_type_on_http_filter_local_reply_status {
  // Continue sending the local reply after onLocalReply has been sent to all filters.
  envoy_dynamic_module_type_on_http_filter_local_reply_status_Continue,
  // Continue sending onLocalReply to all filters, but reset the stream once all filters have been
  // informed rather than sending the local reply.
  envoy_dynamic_module_type_on_http_filter_local_reply_status_ContinueAndResetStream,
} envoy_dynamic_module_type_on_http_filter_local_reply_status;

/**
 * envoy_dynamic_module_type_http_stream_reset_reason represents the reason for resetting the main
 * HTTP stream. This corresponds to `Http::StreamResetReason` in envoy/http/stream_reset_handler.h.
 */
typedef enum envoy_dynamic_module_type_http_filter_stream_reset_reason {
  // If a local codec level reset was sent on the stream.
  envoy_dynamic_module_type_http_filter_stream_reset_reason_LocalReset,
  // If a local codec level refused stream reset was sent on the stream (allowing for retry).
  envoy_dynamic_module_type_http_filter_stream_reset_reason_LocalRefusedStreamReset,
} envoy_dynamic_module_type_http_filter_stream_reset_reason;

// =============================================================================
// HTTP Filter Event Hooks
// =============================================================================

/**
 * envoy_dynamic_module_on_http_filter_config_new is called by the main thread when the http
 * filter config is loaded. The function returns a
 * envoy_dynamic_module_type_http_filter_config_module_ptr for given name and config.
 *
 * @param filter_config_envoy_ptr is the pointer to the DynamicModuleHttpFilterConfig object for the
 * corresponding config.
 * @param name is the name of the filter.
 * @param config is the configuration for the module.
 * @return envoy_dynamic_module_type_http_filter_config_module_ptr is the pointer to the
 * in-module HTTP filter configuration. Returning nullptr indicates a failure to initialize the
 * module. When it fails, the filter configuration will be rejected.
 */
envoy_dynamic_module_type_http_filter_config_module_ptr
envoy_dynamic_module_on_http_filter_config_new(
    envoy_dynamic_module_type_http_filter_config_envoy_ptr filter_config_envoy_ptr,
    envoy_dynamic_module_type_envoy_buffer name, envoy_dynamic_module_type_envoy_buffer config);

/**
 * envoy_dynamic_module_on_http_filter_config_destroy is called when the HTTP filter configuration
 * is destroyed in Envoy. The module should release any resources associated with the corresponding
 * in-module HTTP filter configuration.
 * @param filter_config_ptr is a pointer to the in-module HTTP filter configuration whose
 * corresponding Envoy HTTP filter configuration is being destroyed.
 */
void envoy_dynamic_module_on_http_filter_config_destroy(
    envoy_dynamic_module_type_http_filter_config_module_ptr filter_config_ptr);

/**
 * envoy_dynamic_module_on_http_filter_per_route_config_new is called by the main thread when the
 * http per-route filter config is loaded. The function returns a
 * envoy_dynamic_module_type_http_filter_per_route_config_module_ptr for given name and config.
 *
 * @param name is the name of the filter.
 * @param config is the configuration for the module.
 * @return envoy_dynamic_module_type_http_filter_per_route_config_module_ptr is the pointer to the
 * in-module HTTP filter configuration. Returning nullptr indicates a failure to initialize the
 * module. When it fails, the filter configuration will be rejected.
 */
envoy_dynamic_module_type_http_filter_per_route_config_module_ptr
envoy_dynamic_module_on_http_filter_per_route_config_new(
    envoy_dynamic_module_type_envoy_buffer name, envoy_dynamic_module_type_envoy_buffer config);

/**
 * envoy_dynamic_module_on_http_filter_config_destroy is called when the HTTP per-route filter
 * configuration is destroyed in Envoy. The module should release any resources associated with the
 * corresponding in-module HTTP filter configuration.
 * @param filter_config_ptr is a pointer to the in-module HTTP filter configuration whose
 * corresponding Envoy HTTP filter configuration is being destroyed.
 */
void envoy_dynamic_module_on_http_filter_per_route_config_destroy(
    envoy_dynamic_module_type_http_filter_per_route_config_module_ptr filter_config_ptr);

/**
 * envoy_dynamic_module_on_http_filter_new is called when the HTTP filter is created for each HTTP
 * stream.
 *
 * @param filter_config_ptr is the pointer to the in-module HTTP filter configuration.
 * @param filter_envoy_ptr is the pointer to the DynamicModuleHttpFilter object of the
 * corresponding HTTP filter.
 * @return envoy_dynamic_module_type_http_filter_module_ptr is the pointer to the in-module HTTP
 * filter. Returning nullptr indicates a failure to initialize the module. When it fails, the stream
 * will be closed.
 */
envoy_dynamic_module_type_http_filter_module_ptr envoy_dynamic_module_on_http_filter_new(
    envoy_dynamic_module_type_http_filter_config_module_ptr filter_config_ptr,
    envoy_dynamic_module_type_http_filter_envoy_ptr filter_envoy_ptr);

/**
 * envoy_dynamic_module_on_http_filter_request_headers is called when the HTTP request headers are
 * received.
 *
 * @param filter_envoy_ptr is the pointer to the DynamicModuleHttpFilter object of the
 * corresponding HTTP filter.
 * @param filter_module_ptr is the pointer to the in-module HTTP filter created by
 * envoy_dynamic_module_on_http_filter_new.
 * @param end_of_stream is true if the request headers are the last data.
 * @return envoy_dynamic_module_type_on_http_filter_request_headers_status is the status of the
 * filter.
 */
envoy_dynamic_module_type_on_http_filter_request_headers_status
envoy_dynamic_module_on_http_filter_request_headers(
    envoy_dynamic_module_type_http_filter_envoy_ptr filter_envoy_ptr,
    envoy_dynamic_module_type_http_filter_module_ptr filter_module_ptr, bool end_of_stream);

/**
 * envoy_dynamic_module_on_http_filter_request_body is called when a new data frame of the HTTP
 * request body is received.
 *
 * @param filter_envoy_ptr is the pointer to the DynamicModuleHttpFilter object of the
 * corresponding HTTP filter.
 * @param filter_module_ptr is the pointer to the in-module HTTP filter created by
 * envoy_dynamic_module_on_http_filter_new.
 * @param end_of_stream is true if the request body is the last data.
 * @return envoy_dynamic_module_type_on_http_filter_request_body_status is the status of the filter.
 */
envoy_dynamic_module_type_on_http_filter_request_body_status
envoy_dynamic_module_on_http_filter_request_body(
    envoy_dynamic_module_type_http_filter_envoy_ptr filter_envoy_ptr,
    envoy_dynamic_module_type_http_filter_module_ptr filter_module_ptr, bool end_of_stream);

/**
 * envoy_dynamic_module_on_http_filter_request_trailers is called when the HTTP request trailers are
 * received.
 *
 * @param filter_envoy_ptr is the pointer to the DynamicModuleHttpFilter object of the
 * corresponding HTTP filter.
 * @param filter_module_ptr is the pointer to the in-module HTTP filter created by
 * envoy_dynamic_module_on_http_filter_new.
 * @return envoy_dynamic_module_type_on_http_filter_request_trailers_status is the status of the
 * filter.
 */
envoy_dynamic_module_type_on_http_filter_request_trailers_status
envoy_dynamic_module_on_http_filter_request_trailers(
    envoy_dynamic_module_type_http_filter_envoy_ptr filter_envoy_ptr,
    envoy_dynamic_module_type_http_filter_module_ptr filter_module_ptr);

/**
 * envoy_dynamic_module_on_http_filter_response_headers is called when the HTTP response headers are
 * received.
 *
 * @param filter_envoy_ptr is the pointer to the DynamicModuleHttpFilter object of the
 * corresponding HTTP filter.
 * @param filter_module_ptr is the pointer to the in-module HTTP filter created by
 * envoy_dynamic_module_on_http_filter_new.
 * @param end_of_stream is true if the response headers are the last data.
 * @return envoy_dynamic_module_type_on_http_filter_response_headers_status is the status of the
 * filter.
 */
envoy_dynamic_module_type_on_http_filter_response_headers_status
envoy_dynamic_module_on_http_filter_response_headers(
    envoy_dynamic_module_type_http_filter_envoy_ptr filter_envoy_ptr,
    envoy_dynamic_module_type_http_filter_module_ptr filter_module_ptr, bool end_of_stream);

/**
 * envoy_dynamic_module_on_http_filter_response_body is called when a new data frame of the HTTP
 * response body is received.
 *
 * @param filter_envoy_ptr is the pointer to the DynamicModuleHttpFilter object of the
 * corresponding HTTP filter.
 * @param filter_module_ptr is the pointer to the in-module HTTP filter created by
 * envoy_dynamic_module_on_http_filter_new.
 * @param end_of_stream is true if the response body is the last data.
 * @return envoy_dynamic_module_type_on_http_filter_response_body_status is the status of the
 * filter.
 */
envoy_dynamic_module_type_on_http_filter_response_body_status
envoy_dynamic_module_on_http_filter_response_body(
    envoy_dynamic_module_type_http_filter_envoy_ptr filter_envoy_ptr,
    envoy_dynamic_module_type_http_filter_module_ptr filter_module_ptr, bool end_of_stream);

/**
 * envoy_dynamic_module_on_http_filter_response_trailers is called when the HTTP response trailers
 * are received.
 *
 * @param filter_envoy_ptr is the pointer to the DynamicModuleHttpFilter object of the
 * corresponding HTTP filter.
 * @param filter_module_ptr is the pointer to the in-module HTTP filter created by
 * envoy_dynamic_module_on_http_filter_new.
 * @return envoy_dynamic_module_type_on_http_filter_response_trailers_status is the status of the
 * filter.
 */
envoy_dynamic_module_type_on_http_filter_response_trailers_status
envoy_dynamic_module_on_http_filter_response_trailers(
    envoy_dynamic_module_type_http_filter_envoy_ptr filter_envoy_ptr,
    envoy_dynamic_module_type_http_filter_module_ptr filter_module_ptr);

/**
 * envoy_dynamic_module_on_http_filter_stream_complete is called when the HTTP stream is complete.
 * This is called before envoy_dynamic_module_on_http_filter_destroy and access logs are flushed.
 *
 * @param filter_envoy_ptr is the pointer to the DynamicModuleHttpFilter object of the
 * corresponding HTTP filter.
 * @param filter_module_ptr is the pointer to the in-module HTTP filter created by
 * envoy_dynamic_module_on_http_filter_new.
 */
void envoy_dynamic_module_on_http_filter_stream_complete(
    envoy_dynamic_module_type_http_filter_envoy_ptr filter_envoy_ptr,
    envoy_dynamic_module_type_http_filter_module_ptr filter_module_ptr);

/**
 * envoy_dynamic_module_on_http_filter_destroy is called when the HTTP filter is destroyed for each
 * HTTP stream.
 *
 * @param filter_module_ptr is the pointer to the in-module HTTP filter.
 */
void envoy_dynamic_module_on_http_filter_destroy(
    envoy_dynamic_module_type_http_filter_module_ptr filter_module_ptr);

/**
 * envoy_dynamic_module_on_http_filter_http_callout_done is called when the HTTP callout
 * response is received initiated by a HTTP filter.
 *
 * @param filter_envoy_ptr is the pointer to the DynamicModuleHttpFilter object of the
 * corresponding HTTP filter.
 * @param filter_module_ptr is the pointer to the in-module HTTP filter created by
 * envoy_dynamic_module_on_http_filter_new.
 * @param callout_id is the ID of the callout. This is used to differentiate between multiple
 * calls.
 * @param result is the result of the callout.
 * @param headers is the headers of the response.
 * @param headers_size is the size of the headers.
 * @param body_chunks is the body of the response.
 * @param body_chunks_size is the size of the body.
 *
 * headers and body_chunks are owned by Envoy, and they are guaranteed to be valid until the end of
 * this event hook. They may be null if the callout fails or the response is empty.
 */
void envoy_dynamic_module_on_http_filter_http_callout_done(
    envoy_dynamic_module_type_http_filter_envoy_ptr filter_envoy_ptr,
    envoy_dynamic_module_type_http_filter_module_ptr filter_module_ptr, uint64_t callout_id,
    envoy_dynamic_module_type_http_callout_result result,
    envoy_dynamic_module_type_envoy_http_header* headers, size_t headers_size,
    envoy_dynamic_module_type_envoy_buffer* body_chunks, size_t body_chunks_size);

/**
 * envoy_dynamic_module_on_http_filter_http_stream_headers is called when response headers are
 * received for a streamable HTTP callout stream.
 *
 * @param filter_envoy_ptr is the pointer to the DynamicModuleHttpFilter object of the
 * corresponding HTTP filter.
 * @param filter_module_ptr is the pointer to the in-module HTTP filter created by
 * envoy_dynamic_module_on_http_filter_new.
 * @param stream_id is the handle to the HTTP stream.
 * @param headers is the headers of the response.
 * @param headers_size is the size of the headers.
 * @param end_stream is true if this is the last data in the stream (no body or trailers will
 * follow).
 *
 * headers are owned by Envoy and are guaranteed to be valid until the end of this event hook.
 */
void envoy_dynamic_module_on_http_filter_http_stream_headers(
    envoy_dynamic_module_type_http_filter_envoy_ptr filter_envoy_ptr,
    envoy_dynamic_module_type_http_filter_module_ptr filter_module_ptr, uint64_t stream_id,
    envoy_dynamic_module_type_envoy_http_header* headers, size_t headers_size, bool end_stream);

/**
 * envoy_dynamic_module_on_http_filter_http_stream_data is called when a chunk of response body is
 * received for a streamable HTTP callout stream. This may be called multiple times for a single
 * stream as body chunks arrive.
 *
 * @param filter_envoy_ptr is the pointer to the DynamicModuleHttpFilter object of the
 * corresponding HTTP filter.
 * @param filter_module_ptr is the pointer to the in-module HTTP filter created by
 * envoy_dynamic_module_on_http_filter_new.
 * @param stream_id is the handle to the HTTP stream.
 * @param data is the pointer to the array of buffers containing the body chunk.
 * @param data_count is the number of buffers.
 * @param end_stream is true if this is the last data in the stream (no trailers will follow).
 *
 * data is owned by Envoy and is guaranteed to be valid until the end of this event hook.
 */
void envoy_dynamic_module_on_http_filter_http_stream_data(
    envoy_dynamic_module_type_http_filter_envoy_ptr filter_envoy_ptr,
    envoy_dynamic_module_type_http_filter_module_ptr filter_module_ptr, uint64_t stream_id,
    const envoy_dynamic_module_type_envoy_buffer* data, size_t data_count, bool end_stream);

/**
 * envoy_dynamic_module_on_http_filter_http_stream_trailers is called when response trailers are
 * received for a streamable HTTP callout stream. This is called after headers and any data chunks.
 *
 * @param filter_envoy_ptr is the pointer to the DynamicModuleHttpFilter object of the
 * corresponding HTTP filter.
 * @param filter_module_ptr is the pointer to the in-module HTTP filter created by
 * envoy_dynamic_module_on_http_filter_new.
 * @param stream_id is the handle to the HTTP stream.
 * @param trailers is the trailers of the response.
 * @param trailers_size is the size of the trailers.
 *
 * trailers are owned by Envoy and are guaranteed to be valid until the end of this event hook.
 */
void envoy_dynamic_module_on_http_filter_http_stream_trailers(
    envoy_dynamic_module_type_http_filter_envoy_ptr filter_envoy_ptr,
    envoy_dynamic_module_type_http_filter_module_ptr filter_module_ptr, uint64_t stream_id,
    envoy_dynamic_module_type_envoy_http_header* trailers, size_t trailers_size);

/**
 * envoy_dynamic_module_on_http_filter_http_stream_complete is called when a streamable HTTP
 * callout stream completes successfully. This is called after all headers, data, and trailers have
 * been received.
 *
 * @param filter_envoy_ptr is the pointer to the DynamicModuleHttpFilter object of the
 * corresponding HTTP filter.
 * @param filter_module_ptr is the pointer to the in-module HTTP filter created by
 * envoy_dynamic_module_on_http_filter_new.
 * @param stream_id is the handle to the HTTP stream.
 *
 * After this callback, the stream is automatically cleaned up and stream_ptr becomes invalid.
 */
void envoy_dynamic_module_on_http_filter_http_stream_complete(
    envoy_dynamic_module_type_http_filter_envoy_ptr filter_envoy_ptr,
    envoy_dynamic_module_type_http_filter_module_ptr filter_module_ptr, uint64_t stream_id);

/**
 * envoy_dynamic_module_on_http_filter_http_stream_reset is called when a streamable HTTP callout
 * stream is reset or fails. This may be called instead of the complete callback if the stream
 * encounters an error.
 *
 * @param filter_envoy_ptr is the pointer to the DynamicModuleHttpFilter object of the
 * corresponding HTTP filter.
 * @param filter_module_ptr is the pointer to the in-module HTTP filter created by
 * envoy_dynamic_module_on_http_filter_new.
 * @param stream_id is the handle to the HTTP stream.
 * @param reason is the reason for the stream reset.
 *
 * After this callback, the stream is automatically cleaned up and stream_ptr becomes invalid.
 */
void envoy_dynamic_module_on_http_filter_http_stream_reset(
    envoy_dynamic_module_type_http_filter_envoy_ptr filter_envoy_ptr,
    envoy_dynamic_module_type_http_filter_module_ptr filter_module_ptr, uint64_t stream_id,
    envoy_dynamic_module_type_http_stream_reset_reason reason);

/**
 * envoy_dynamic_module_on_http_filter_scheduled is called when the HTTP filter is scheduled
 * to be executed on the worker thread where the HTTP filter is running with
 * envoy_dynamic_module_callback_http_filter_scheduler_commit callback.
 *
 * @param filter_envoy_ptr is the pointer to the DynamicModuleHttpFilter object of the
 * corresponding HTTP filter.
 * @param filter_module_ptr is the pointer to the in-module HTTP filter created by
 * envoy_dynamic_module_on_http_filter_new.
 * @param event_id is the ID of the event passed to
 * envoy_dynamic_module_callback_http_filter_scheduler_commit.
 */
void envoy_dynamic_module_on_http_filter_scheduled(
    envoy_dynamic_module_type_http_filter_envoy_ptr filter_envoy_ptr,
    envoy_dynamic_module_type_http_filter_module_ptr filter_module_ptr, uint64_t event_id);

/**
 * envoy_dynamic_module_on_http_filter_config_scheduled is called when the HTTP filter
 * configuration is scheduled to be executed on the main thread with
 * envoy_dynamic_module_callback_http_filter_config_scheduler_commit callback.
 *
 * @param filter_config_envoy_ptr is the pointer to the DynamicModuleHttpFilterConfig object.
 * @param filter_config_ptr is the pointer to the in-module HTTP filter configuration created by
 * envoy_dynamic_module_on_http_filter_config_new.
 * @param event_id is the ID of the event passed to
 * envoy_dynamic_module_callback_http_filter_config_scheduler_commit.
 */
void envoy_dynamic_module_on_http_filter_config_scheduled(
    envoy_dynamic_module_type_http_filter_config_envoy_ptr filter_config_envoy_ptr,
    envoy_dynamic_module_type_http_filter_config_module_ptr filter_config_ptr, uint64_t event_id);

/**
 * envoy_dynamic_module_on_http_filter_config_http_callout_done is called when the HTTP callout
 * response is received for a callout initiated by an HTTP filter configuration.
 *
 * @param filter_config_envoy_ptr is the pointer to the DynamicModuleHttpFilterConfig object.
 * @param filter_config_ptr is the pointer to the in-module HTTP filter configuration created by
 * envoy_dynamic_module_on_http_filter_config_new.
 * @param callout_id is the ID of the callout. This is used to differentiate between multiple
 * calls.
 * @param result is the result of the callout.
 * @param headers is the headers of the response.
 * @param headers_size is the size of the headers.
 * @param body_chunks is the body of the response.
 * @param body_chunks_size is the size of the body.
 *
 * headers and body_chunks are owned by Envoy, and they are guaranteed to be valid until the end of
 * this event hook. They may be null if the callout fails or the response is empty.
 */
void envoy_dynamic_module_on_http_filter_config_http_callout_done(
    envoy_dynamic_module_type_http_filter_config_envoy_ptr filter_config_envoy_ptr,
    envoy_dynamic_module_type_http_filter_config_module_ptr filter_config_ptr, uint64_t callout_id,
    envoy_dynamic_module_type_http_callout_result result,
    envoy_dynamic_module_type_envoy_http_header* headers, size_t headers_size,
    envoy_dynamic_module_type_envoy_buffer* body_chunks, size_t body_chunks_size);

/**
 * envoy_dynamic_module_on_http_filter_config_http_stream_headers is called when response headers
 * are received for a streamable HTTP callout started from an HTTP filter configuration.
 *
 * @param filter_config_envoy_ptr is the pointer to the DynamicModuleHttpFilterConfig object.
 * @param filter_config_ptr is the pointer to the in-module HTTP filter configuration created by
 * envoy_dynamic_module_on_http_filter_config_new.
 * @param stream_id is the handle to the HTTP stream.
 * @param headers is the headers of the response.
 * @param headers_size is the size of the headers.
 * @param end_stream is true if this is the last data in the stream (no body or trailers will
 * follow).
 *
 * headers are owned by Envoy and are guaranteed to be valid until the end of this event hook.
 */
void envoy_dynamic_module_on_http_filter_config_http_stream_headers(
    envoy_dynamic_module_type_http_filter_config_envoy_ptr filter_config_envoy_ptr,
    envoy_dynamic_module_type_http_filter_config_module_ptr filter_config_ptr, uint64_t stream_id,
    envoy_dynamic_module_type_envoy_http_header* headers, size_t headers_size, bool end_stream);

/**
 * envoy_dynamic_module_on_http_filter_config_http_stream_data is called when a chunk of response
 * body is received for a streamable HTTP callout started from an HTTP filter configuration. This
 * may be called multiple times for a single stream as body chunks arrive.
 *
 * @param filter_config_envoy_ptr is the pointer to the DynamicModuleHttpFilterConfig object.
 * @param filter_config_ptr is the pointer to the in-module HTTP filter configuration created by
 * envoy_dynamic_module_on_http_filter_config_new.
 * @param stream_id is the handle to the HTTP stream.
 * @param data is the pointer to the array of buffers containing the body chunk.
 * @param data_count is the number of buffers.
 * @param end_stream is true if this is the last data in the stream (no trailers will follow).
 *
 * data is owned by Envoy and is guaranteed to be valid until the end of this event hook.
 */
void envoy_dynamic_module_on_http_filter_config_http_stream_data(
    envoy_dynamic_module_type_http_filter_config_envoy_ptr filter_config_envoy_ptr,
    envoy_dynamic_module_type_http_filter_config_module_ptr filter_config_ptr, uint64_t stream_id,
    const envoy_dynamic_module_type_envoy_buffer* data, size_t data_count, bool end_stream);

/**
 * envoy_dynamic_module_on_http_filter_config_http_stream_trailers is called when response trailers
 * are received for a streamable HTTP callout started from an HTTP filter configuration.
 *
 * @param filter_config_envoy_ptr is the pointer to the DynamicModuleHttpFilterConfig object.
 * @param filter_config_ptr is the pointer to the in-module HTTP filter configuration created by
 * envoy_dynamic_module_on_http_filter_config_new.
 * @param stream_id is the handle to the HTTP stream.
 * @param trailers is the trailers of the response.
 * @param trailers_size is the size of the trailers.
 *
 * trailers are owned by Envoy and are guaranteed to be valid until the end of this event hook.
 */
void envoy_dynamic_module_on_http_filter_config_http_stream_trailers(
    envoy_dynamic_module_type_http_filter_config_envoy_ptr filter_config_envoy_ptr,
    envoy_dynamic_module_type_http_filter_config_module_ptr filter_config_ptr, uint64_t stream_id,
    envoy_dynamic_module_type_envoy_http_header* trailers, size_t trailers_size);

/**
 * envoy_dynamic_module_on_http_filter_config_http_stream_complete is called when a streamable HTTP
 * callout stream started from an HTTP filter configuration completes successfully. This is called
 * after all headers, data, and trailers have been received.
 *
 * @param filter_config_envoy_ptr is the pointer to the DynamicModuleHttpFilterConfig object.
 * @param filter_config_ptr is the pointer to the in-module HTTP filter configuration created by
 * envoy_dynamic_module_on_http_filter_config_new.
 * @param stream_id is the handle to the HTTP stream.
 *
 * After this callback, the stream is automatically cleaned up and stream_id becomes invalid.
 */
void envoy_dynamic_module_on_http_filter_config_http_stream_complete(
    envoy_dynamic_module_type_http_filter_config_envoy_ptr filter_config_envoy_ptr,
    envoy_dynamic_module_type_http_filter_config_module_ptr filter_config_ptr, uint64_t stream_id);

/**
 * envoy_dynamic_module_on_http_filter_config_http_stream_reset is called when a streamable HTTP
 * callout stream started from an HTTP filter configuration is reset or fails.
 *
 * @param filter_config_envoy_ptr is the pointer to the DynamicModuleHttpFilterConfig object.
 * @param filter_config_ptr is the pointer to the in-module HTTP filter configuration created by
 * envoy_dynamic_module_on_http_filter_config_new.
 * @param stream_id is the handle to the HTTP stream.
 * @param reason is the reason for the stream reset.
 *
 * After this callback, the stream is automatically cleaned up and stream_id becomes invalid.
 */
void envoy_dynamic_module_on_http_filter_config_http_stream_reset(
    envoy_dynamic_module_type_http_filter_config_envoy_ptr filter_config_envoy_ptr,
    envoy_dynamic_module_type_http_filter_config_module_ptr filter_config_ptr, uint64_t stream_id,
    envoy_dynamic_module_type_http_stream_reset_reason reason);

/**
 * envoy_dynamic_module_on_http_filter_downstream_above_write_buffer_high_watermark is called when
 * the buffer for the downstream stream goes over the high watermark for a terminal filter. This may
 * be called multiple times, in which case envoy_dynamic_module_on_above_write_buffer_low_watermark
 * will be called an equal number of times until the write buffer is completely drained below the
 * low watermark.
 *
 * @param filter_envoy_ptr is the pointer to the DynamicModuleHttpFilter object of the
 * corresponding HTTP filter.
 * @param filter_module_ptr is the pointer to the in-module HTTP filter created by
 * envoy_dynamic_module_on_http_filter_new.
 */
void envoy_dynamic_module_on_http_filter_downstream_above_write_buffer_high_watermark(
    envoy_dynamic_module_type_http_filter_envoy_ptr filter_envoy_ptr,
    envoy_dynamic_module_type_http_filter_module_ptr filter_module_ptr);

/**
 * envoy_dynamic_module_on_http_filter_downstream_below_write_buffer_low_watermark is called when
 * any buffer for the response stream goes from over its high watermark to under its low watermark
 * for a terminal filter.
 *
 * @param filter_envoy_ptr is the pointer to the DynamicModuleHttpFilter object of the
 * corresponding HTTP filter.
 * @param filter_module_ptr is the pointer to the in-module HTTP filter created by
 * envoy_dynamic_module_on_http_filter_new.
 */
void envoy_dynamic_module_on_http_filter_downstream_below_write_buffer_low_watermark(
    envoy_dynamic_module_type_http_filter_envoy_ptr filter_envoy_ptr,
    envoy_dynamic_module_type_http_filter_module_ptr filter_module_ptr);

/**
 * envoy_dynamic_module_on_http_filter_local_reply is called when sendLocalReply is invoked on the
 * HTTP stream. This allows filters to be notified when a local reply is being generated, which is
 * useful for logging local errors or modifying local reply behavior.
 *
 * The return value controls what happens after all filters have been informed:
 * - Continue: Send the local reply as normal.
 * - ContinueAndResetStream: Reset the stream instead of sending the local reply.
 *
 * @param filter_envoy_ptr is the pointer to the DynamicModuleHttpFilter object of the
 * corresponding HTTP filter.
 * @param filter_module_ptr is the pointer to the in-module HTTP filter created by
 * envoy_dynamic_module_on_http_filter_new.
 * @param response_code is the HTTP response code for the local reply.
 * @param details is the response code details string.
 * @param reset_imminent is true if a reset will occur rather than the local reply.
 * @return envoy_dynamic_module_type_on_http_filter_local_reply_status indicating the action to take
 * after the local reply hook completes.
 */
envoy_dynamic_module_type_on_http_filter_local_reply_status
envoy_dynamic_module_on_http_filter_local_reply(
    envoy_dynamic_module_type_http_filter_envoy_ptr filter_envoy_ptr,
    envoy_dynamic_module_type_http_filter_module_ptr filter_module_ptr, uint32_t response_code,
    envoy_dynamic_module_type_envoy_buffer details, bool reset_imminent);

// =============================================================================
// HTTP Filter Callbacks
// =============================================================================

// ----------------------------- Metrics callbacks -----------------------------

/**
 * envoy_dynamic_module_callback_http_filter_config_define_counter is called by the module
 * during initialization to create a template for generating Stats::Counters with the given name and
 * labels during the lifecycle of the module.
 *
 * @param filter_config_envoy_ptr is the pointer to the DynamicModuleHttpFilterConfig in which the
 * counter will be defined.
 * @param name is the name of the counter to be defined.
 * @param label_names is the labels of the counter to be defined.
 * NOTE: label names could be null if the label_names_length is 0.
 * @param label_names_length is the length of the label_names.
 * NOTE: label_names_length could be 0 if there are no labels.
 * @param counter_id_ptr where the opaque ID that represents a unique metric will be stored. This
 * can be passed to envoy_dynamic_module_callback_http_filter_increment_counter together with
 * filter_envoy_ptr created from filter_config_envoy_ptr.
 * @return the result of the operation.
 */
envoy_dynamic_module_type_metrics_result
envoy_dynamic_module_callback_http_filter_config_define_counter(
    envoy_dynamic_module_type_http_filter_config_envoy_ptr filter_config_envoy_ptr,
    envoy_dynamic_module_type_module_buffer name,
    envoy_dynamic_module_type_module_buffer* label_names, size_t label_names_length,
    size_t* counter_id_ptr);

/**
 * envoy_dynamic_module_callback_http_filter_increment_counter is called by the module to
 * increment a previously defined counter.
 *
 * @param filter_envoy_ptr is the pointer to the DynamicModuleHttpFilter object.
 * @param id is the ID of the counter previously defined using the config that created
 * filter_envoy_ptr
 * @param label_values is the values of the labels to be incremented.
 * NOTE: label_values could be null if the label_values_length is 0.
 * @param label_values_length is the length of the label_values.
 * NOTE: label_values_length could be 0 if there are no labels. **THE LENGTH MUST MATCH THE
 * LABEL NAMES DEFINED DURING COUNTER DEFINITION.**
 * @param value is the value to increment the counter by.
 * @return the result of the operation.
 */
envoy_dynamic_module_type_metrics_result
envoy_dynamic_module_callback_http_filter_increment_counter(
    envoy_dynamic_module_type_http_filter_envoy_ptr filter_envoy_ptr, size_t id,
    envoy_dynamic_module_type_module_buffer* label_values, size_t label_values_length,
    uint64_t value);

/**
 * envoy_dynamic_module_callback_http_filter_config_define_gauge is called by the module during
 * initialization to create a template for generating Stats::Gauges with the given name and labels
 * during the lifecycle of the module.
 *
 * @param filter_config_envoy_ptr is the pointer to the DynamicModuleHttpFilterConfig in which the
 * gauge will be defined.
 * @param name is the name of the gauge to be defined.
 * @param label_names is the labels of the gauge to be defined.
 * NOTE: label names could be null if the label_names_length is 0.
 * @param label_names_length is the length of the label_names.
 * NOTE: label_names_length could be 0 if there are no labels.
 * @param gauge_id_ptr where the opaque ID that represents a unique metric will be stored. This can
 * be passed to envoy_dynamic_module_callback_http_filter_increment_gauge together with
 * filter_envoy_ptr created from filter_config_envoy_ptr.
 * @return the result of the operation.
 */
envoy_dynamic_module_type_metrics_result
envoy_dynamic_module_callback_http_filter_config_define_gauge(
    envoy_dynamic_module_type_http_filter_config_envoy_ptr filter_config_envoy_ptr,
    envoy_dynamic_module_type_module_buffer name,
    envoy_dynamic_module_type_module_buffer* label_names, size_t label_names_length,
    size_t* gauge_id_ptr);

/**
 * envoy_dynamic_module_callback_http_filter_increment_gauge is called by the module to increase
 * the value of a previously defined gauge.
 *
 * @param filter_envoy_ptr is the pointer to the DynamicModuleHttpFilter object.
 * @param id is the ID of the gauge previously defined using the config that created
 * filter_envoy_ptr
 * @param label_values is the values of the labels to be increased.
 * NOTE: label_values could be null if the label_values_length is 0.
 * @param label_values_length is the length of the label_values.
 * NOTE: label_values_length could be 0 if there are no labels. **THE LENGTH MUST MATCH THE
 * LABEL NAMES DEFINED DURING GAUGE DEFINITION.**
 * @param value is the value to increase the gauge by.
 * @return the result of the operation.
 */
envoy_dynamic_module_type_metrics_result envoy_dynamic_module_callback_http_filter_increment_gauge(
    envoy_dynamic_module_type_http_filter_envoy_ptr filter_envoy_ptr, size_t id,
    envoy_dynamic_module_type_module_buffer* label_values, size_t label_values_length,
    uint64_t value);

/**
 * envoy_dynamic_module_callback_http_filter_decrement_gauge is called by the module to decrease
 * the value of a previously defined gauge.
 *
 * @param filter_envoy_ptr is the pointer to the DynamicModuleHttpFilter object.
 * @param id is the ID of the gauge previously defined using the config that created
 * filter_envoy_ptr
 * @param label_values is the values of the labels to be decreased.
 * NOTE: label_values could be null if the label_values_length is 0.
 * @param label_values_length is the length of the label_values.
 * NOTE: label_values_length could be 0 if there are no labels. **THE LENGTH MUST MATCH THE
 * LABEL NAMES DEFINED DURING GAUGE DEFINITION.**
 * @param value is the value to decrease the gauge by.
 * @return the result of the operation.
 */
envoy_dynamic_module_type_metrics_result envoy_dynamic_module_callback_http_filter_decrement_gauge(
    envoy_dynamic_module_type_http_filter_envoy_ptr filter_envoy_ptr, size_t id,
    envoy_dynamic_module_type_module_buffer* label_values, size_t label_values_length,
    uint64_t value);

/**
 * envoy_dynamic_module_callback_http_filter_set_gauge is called by the module to set the value
 * of a previously defined gauge.
 *
 * @param filter_envoy_ptr is the pointer to the DynamicModuleHttpFilter object.
 * @param id is the ID of the gauge previously defined using the config that created
 * filter_envoy_ptr
 * @param label_values is the values of the labels to be set.
 * NOTE: label_values could be null if the label_values_length is 0.
 * @param label_values_length is the length of the label_values.
 * NOTE: label_values_length could be 0 if there are no labels. **THE LENGTH MUST MATCH THE
 * LABEL NAMES DEFINED DURING GAUGE DEFINITION.**
 * @param value is the value to set the gauge to.
 * @return the result of the operation.
 */
envoy_dynamic_module_type_metrics_result envoy_dynamic_module_callback_http_filter_set_gauge(
    envoy_dynamic_module_type_http_filter_envoy_ptr filter_envoy_ptr, size_t id,
    envoy_dynamic_module_type_module_buffer* label_values, size_t label_values_length,
    uint64_t value);

/**
 * envoy_dynamic_module_callback_http_filter_config_define_histogram is called by the module
 * during initialization to create a template for generating Stats::Histograms with the given name
 * and labels during the lifecycle of the module.
 *
 * @param filter_config_envoy_ptr is the pointer to the DynamicModuleHttpFilterConfig in which the
 * histogram will be defined.
 * @param name is the name of the histogram to be defined.
 * @param label_names is the labels of the histogram to be defined.
 * NOTE: label names could be null if the label_names_length is 0.
 * @param label_names_length is the length of the label_names.
 * NOTE: label_names_length could be 0 if there are no labels.
 * @param histogram_id_ptr where the opaque ID that represents a unique metric will be stored. This
 * can be passed to envoy_dynamic_module_callback_http_filter_record_histogram_value_vec together
 * with filter_envoy_ptr created from filter_config_envoy_ptr.
 * @return the result of the operation.
 */
envoy_dynamic_module_type_metrics_result
envoy_dynamic_module_callback_http_filter_config_define_histogram(
    envoy_dynamic_module_type_http_filter_config_envoy_ptr filter_config_envoy_ptr,
    envoy_dynamic_module_type_module_buffer name,
    envoy_dynamic_module_type_module_buffer* label_names, size_t label_names_length,
    size_t* histogram_id_ptr);
/**
 * envoy_dynamic_module_callback_http_filter_record_histogram_value is called by the module to
 * record a value in a previously defined histogram.
 *
 * @param filter_envoy_ptr is the pointer to the DynamicModuleHttpFilter object.
 * @param id is the ID of the histogram previously defined using the config that created
 * filter_envoy_ptr
 * @param label_values is the values of the labels to be recorded.
 * NOTE: label_values could be null if the label_values_length is 0.
 * @param label_values_length is the length of the label_values.
 * NOTE: label_values_length could be 0 if there are no labels. **THE LENGTH MUST MATCH THE
 * LABEL NAMES DEFINED DURING HISTOGRAM DEFINITION.**
 * @param value is the value to record in the histogram.
 * @return the result of the operation.
 */
envoy_dynamic_module_type_metrics_result
envoy_dynamic_module_callback_http_filter_record_histogram_value(
    envoy_dynamic_module_type_http_filter_envoy_ptr filter_envoy_ptr, size_t id,
    envoy_dynamic_module_type_module_buffer* label_values, size_t label_values_length,
    uint64_t value);

// ---------------------- HTTP Header/Trailer callbacks ------------------------

/**
 * envoy_dynamic_module_callback_http_get_header is called by the module to get the
 * value of the header with the given key. Since a header can have multiple values, the
 * index is used to get the specific value. This returns the number of values for the given key, so
 * it can be used to iterate over all values by starting from 0 and incrementing the index until the
 * return value.
 *
 * PRECONDITION: Envoy does not check the validity of the key as well as the result_buffer_ptr
 * and result_buffer_length_ptr. The module must ensure that these values are valid, e.g.
 * non-null pointers.
 *
 * @param filter_envoy_ptr is the pointer to the DynamicModuleHttpFilter object of the
 * corresponding HTTP filter.
 * @param header_type is the type of the header map to get the header from (request/response
 * headers/trailers).
 * @param key is the key of the header.
 * @param result_buffer is the buffer where the value will be stored. If the key does not exist or
 * the index is out of range, this will be set to a null buffer (length 0).
 * @param index is the index of the header value in the list of values for the given key.
 * @param optional_size is the pointer to the variable where the number of values for the given key
 * will be stored.
 * NOTE: This parameter is optional and can be null if the module does not need this information.
 * @return true if the operation is successful, false otherwise.
 *
 * Note that a header value is not guaranteed to be a valid UTF-8 string. The module must be careful
 * when interpreting the value as a string in the language of the module.
 *
 * The buffer pointed by the pointer stored in result_buffer_ptr is owned by Envoy, and they are
 * guaranteed to be valid until the end of the current event hook unless the setter callback is
 * called.
 */
bool envoy_dynamic_module_callback_http_get_header(
    envoy_dynamic_module_type_http_filter_envoy_ptr filter_envoy_ptr,
    envoy_dynamic_module_type_http_header_type header_type,
    envoy_dynamic_module_type_module_buffer key,
    envoy_dynamic_module_type_envoy_buffer* result_buffer, size_t index, size_t* optional_size);

/**
 * envoy_dynamic_module_callback_http_get_headers_size is called by the module to get the
 * number of headers. Combined with envoy_dynamic_module_callback_http_get_headers,
 * this can be used to iterate over all request headers.
 *
 * @param filter_envoy_ptr is the pointer to the DynamicModuleHttpFilter object of the
 * corresponding HTTP filter.
 * @param header_type is the type of the header map to get the size from (request/response
 * headers/trailers).
 * @return the number of headers. 0 if there are no headers or headers could not be retrieved.
 */
size_t envoy_dynamic_module_callback_http_get_headers_size(
    envoy_dynamic_module_type_http_filter_envoy_ptr filter_envoy_ptr,
    envoy_dynamic_module_type_http_header_type header_type);

/**
 * envoy_dynamic_module_callback_http_get_headers is called by the module to get all the
 * headers. The headers are returned as an array of
 * envoy_dynamic_module_type_envoy_http_header.
 *
 * PRECONDITION: The module must ensure that the result_headers is valid and has enough length to
 * store all the headers. The module can use
 * envoy_dynamic_module_callback_http_get_headers_size to get the number of headers before
 * calling this function.
 *
 * @param filter_envoy_ptr is the pointer to the DynamicModuleHttpFilter object of the
 * corresponding HTTP filter.
 * @param header_type is the type of the header map to get the headers from (request/response
 * headers/trailers).
 * @param result_headers is the pointer to the array of envoy_dynamic_module_type_envoy_http_header
 * where the headers will be stored. The lifetime of the buffer of key and value of each header is
 * guaranteed until the end of the current event hook unless the setter callback are called.
 * @return true if the operation is successful, false otherwise.
 */
bool envoy_dynamic_module_callback_http_get_headers(
    envoy_dynamic_module_type_http_filter_envoy_ptr filter_envoy_ptr,
    envoy_dynamic_module_type_http_header_type header_type,
    envoy_dynamic_module_type_envoy_http_header* result_headers);

/**
 * envoy_dynamic_module_callback_http_add_header is called by the module to add
 * the value of the header with the given key. If the header does not exist, it will be
 * created. If the header already exists, all existing values will be removed and the new value will
 * be set. When the given value is null, the header will be removed if the key exists.
 *
 * @param filter_envoy_ptr is the pointer to the DynamicModuleHttpFilter object of the
 * corresponding HTTP filter.
 * @param header_type is the type of the header map to add the header to (request/response
 * headers/trailers).
 * @param key is the key of the header.
 * @param value is the pointer to the buffer of the value. It can be null to remove the header.
 * @return true if the operation is successful, false otherwise.
 *
 * Note that this only adds the header to the underlying Envoy object. Whether or not the header is
 * actually sent to the upstream depends on the phase of the execution and subsequent
 * filters. In other words, returning true from this function does not guarantee that the header
 * will be sent to the upstream.
 */
bool envoy_dynamic_module_callback_http_add_header(
    envoy_dynamic_module_type_http_filter_envoy_ptr filter_envoy_ptr,
    envoy_dynamic_module_type_http_header_type header_type,
    envoy_dynamic_module_type_module_buffer key, envoy_dynamic_module_type_module_buffer value);

/**
 * envoy_dynamic_module_callback_http_set_header is called by the module to set
 * the value of the header with the given key. If the header does not exist, it will be
 * created. If the header already exists, all existing values will be removed and the new value will
 * be set. When the given value is null, the header will be removed if the key exists.
 *
 * @param filter_envoy_ptr is the pointer to the DynamicModuleHttpFilter object of the
 * corresponding HTTP filter.
 * @param header_type is the type of the header map to set the header to (request/response
 * headers/trailers).
 * @param key is the key of the header.
 * @param value is the pointer to the buffer of the value. It can be null to remove the header.
 * @return true if the operation is successful, false otherwise.
 *
 * Note that this only sets the header to the underlying Envoy object. Whether or not the header is
 * actually sent to the upstream depends on the phase of the execution and subsequent
 * filters. In other words, returning true from this function does not guarantee that the header
 * will be sent to the upstream.
 */
bool envoy_dynamic_module_callback_http_set_header(
    envoy_dynamic_module_type_http_filter_envoy_ptr filter_envoy_ptr,
    envoy_dynamic_module_type_http_header_type header_type,
    envoy_dynamic_module_type_module_buffer key, envoy_dynamic_module_type_module_buffer value);

// ---------------------- HTTP local response callbacks ------------------------

/**
 * envoy_dynamic_module_callback_http_send_response is called by the module to send the response
 * to the downstream.
 *
 * @param filter_envoy_ptr is the pointer to the DynamicModuleHttpFilter object of the
 * corresponding HTTP filter.
 * @param status_code is the status code of the response.
 * @param headers_vector is the array of envoy_dynamic_module_type_module_http_header that contains
 * the headers of the response.
 * @param headers_vector_size is the size of the headers_vector.
 * @param body is the body of the response.
 * @param details is the response code details of the response.
 * The response code details is an optional short string that provides additional information about
 * why this response code was sent like "rate_limited". It is typically used for logging purposes.
 * This is optional and can be null.
 */
void envoy_dynamic_module_callback_http_send_response(
    envoy_dynamic_module_type_http_filter_envoy_ptr filter_envoy_ptr, uint32_t status_code,
    envoy_dynamic_module_type_module_http_header* headers_vector, size_t headers_vector_size,
    envoy_dynamic_module_type_module_buffer body, envoy_dynamic_module_type_module_buffer details);

/**
 * envoy_dynamic_module_callback_http_send_response_headers is called by the module to send the
 * response headers to the downstream, optionally ending the stream. Necessary pseudo headers
 * such as :status should be present.
 *
 * @param filter_envoy_ptr is the pointer to the DynamicModuleHttpFilter object of the
 * corresponding HTTP filter.
 * @param headers_vector is the array of envoy_dynamic_module_type_module_http_header that contains
 * the headers of the response.
 * @param headers_vector_size is the size of the headers_vector.
 * @param end_stream is a boolean indicating whether to end the stream.
 */
void envoy_dynamic_module_callback_http_send_response_headers(
    envoy_dynamic_module_type_http_filter_envoy_ptr filter_envoy_ptr,
    envoy_dynamic_module_type_module_http_header* headers_vector, size_t headers_vector_size,
    bool end_stream);

/**
 * envoy_dynamic_module_callback_http_send_response_data is called by the module to send response
 * data to the downstream, optionally ending the stream.
 *
 * @param filter_envoy_ptr is the pointer to the DynamicModuleHttpFilter object of the
 * corresponding HTTP filter.
 * @param data is the body data of the response.
 * @param end_stream is a boolean indicating whether to end the stream.
 */
void envoy_dynamic_module_callback_http_send_response_data(
    envoy_dynamic_module_type_http_filter_envoy_ptr filter_envoy_ptr,
    envoy_dynamic_module_type_module_buffer data, bool end_stream);

/**
 * envoy_dynamic_module_callback_http_send_response_trailers is called by the module to send the
 * response trailers to the downstream, ending the stream.
 *
 * @param filter_envoy_ptr is the pointer to the DynamicModuleHttpFilter object of the
 * corresponding HTTP filter.
 * @param trailers_vector is the array of envoy_dynamic_module_type_module_http_header that contains
 * the trailers of the response.
 * @param trailers_vector_size is the size of the trailers_vector.
 */
void envoy_dynamic_module_callback_http_send_response_trailers(
    envoy_dynamic_module_type_http_filter_envoy_ptr filter_envoy_ptr,
    envoy_dynamic_module_type_module_http_header* trailers_vector, size_t trailers_vector_size);

// ------------------- HTTP Request/Response body callbacks --------------------

/**
 * NOTE: Envoy will handle the request/response as a stream of data. Therefore, the body may not be
 * available in its entirety before the end of stream flag is set. The Envoy will provides both the
 * received body (body pieces received in the latest event) and the buffered body (body pieces
 * buffered so far) to the module. The module should be aware of this distinction when processing
 * the body.
 *
 * NOTE: The received body could only be available during the request/response body
 * event hooks (the envoy_dynamic_module_on_http_filter_request_body and
 * envoy_dynamic_module_on_http_filter_response_body).
 * Outside of these hooks, the received body will be unavailable.
 *
 * NOTE: The buffered body, however, is always available. But only the latest data processing filter
 * in the filter chain could modify the buffered body. That is say for a given filter X, filter X
 * can safely modify the buffered body if and only if the filters following filter X in the filter
 * chain have not yet accessed the body.
 */

/**
 * envoy_dynamic_module_callback_http_get_body_size is called by the module
 * to get the total bytes of body.
 *
 * @param filter_envoy_ptr is the pointer to the DynamicModuleHttpFilter object of the
 * corresponding HTTP filter.
 * @param body_type is the type of the body to get the size from (request/response,
 * received/buffered body).
 * @return the size of the body in bytes. 0 if the body is not available or empty.
 */
size_t envoy_dynamic_module_callback_http_get_body_size(
    envoy_dynamic_module_type_http_filter_envoy_ptr filter_envoy_ptr,
    envoy_dynamic_module_type_http_body_type body_type);

/**
 * envoy_dynamic_module_callback_http_get_body_chunks is called by the module to
 * get the body as a vector of buffers. The body is returned as an array of
 * envoy_dynamic_module_type_envoy_buffer.
 *
 * PRECONDITION: The module must ensure that the result_buffer_vector is valid and has enough length
 * to store all the buffers. The module can use
 * envoy_dynamic_module_callback_http_get_body_chunks_size to get the number of
 * buffers before calling this function.
 *
 * @param filter_envoy_ptr is the pointer to the DynamicModuleHttpFilter object of the
 * corresponding HTTP filter.
 * @param body_type is the type of the body to get the buffers from (request/response,
 * received/buffered body).
 * @param result_buffer_vector is the pointer to the array of envoy_dynamic_module_type_envoy_buffer
 * where the buffers of the body will be stored. The lifetime of the buffer is guaranteed until the
 * end of the current event hook unless the setter callback is called.
 * @return true if the body is available, false otherwise.
 */
bool envoy_dynamic_module_callback_http_get_body_chunks(
    envoy_dynamic_module_type_http_filter_envoy_ptr filter_envoy_ptr,
    envoy_dynamic_module_type_http_body_type body_type,
    envoy_dynamic_module_type_envoy_buffer* result_buffer_vector);

/**
 * envoy_dynamic_module_callback_http_get_body_chunks_size is called by the module
 * to get the number of buffers in the current request body. Combined with
 * envoy_dynamic_module_callback_http_get_body_chunks, this can be used to iterate
 * over all buffers in the body.
 *
 * @param filter_envoy_ptr is the pointer to the DynamicModuleHttpFilter object of the
 * corresponding HTTP filter.
 * @param body_type is the type of the body to get the number of buffers from (request/response,
 * received/buffered body).
 * @return the number of buffers in the body. 0 if the body is not available or empty.
 */
size_t envoy_dynamic_module_callback_http_get_body_chunks_size(
    envoy_dynamic_module_type_http_filter_envoy_ptr filter_envoy_ptr,
    envoy_dynamic_module_type_http_body_type body_type);

/**
 * envoy_dynamic_module_callback_http_append_body is called by the module to append
 * the given data to the end of the body.
 *
 * @param filter_envoy_ptr is the pointer to the DynamicModuleHttpFilter object of the
 * corresponding HTTP filter.
 * @param body_type is the type of the body to append to (request/response,
 * received/buffered body).
 * @param data is the body data to be appended.
 * @return true if the body is available, false otherwise.
 */
bool envoy_dynamic_module_callback_http_append_body(
    envoy_dynamic_module_type_http_filter_envoy_ptr filter_envoy_ptr,
    envoy_dynamic_module_type_http_body_type body_type,
    envoy_dynamic_module_type_module_buffer data);

/**
 * envoy_dynamic_module_callback_http_drain_body is called by the module to drain
 * the given number of bytes from the body. If the number of bytes to drain is
 * greater than the size of the body, the whole body will be drained.
 *
 * @param filter_envoy_ptr is the pointer to the DynamicModuleHttpFilter object of the
 * corresponding HTTP filter.
 * @param body_type is the type of the body to drain from (request/response,
 * received/buffered body).
 * @param number_of_bytes is the number of bytes to drain.
 * @return true if the body is available, false otherwise.
 */
bool envoy_dynamic_module_callback_http_drain_body(
    envoy_dynamic_module_type_http_filter_envoy_ptr filter_envoy_ptr,
    envoy_dynamic_module_type_http_body_type body_type, size_t number_of_bytes);

/**
 * envoy_dynamic_module_callback_http_received_buffered_request_body is called by the module to
 * check if the latest received request body actually is the previously buffered request body.
 *
 * For example, the previous filter X have stopped the filter chain and buffered the request body.
 * Then X resumes the filter chain after receiving the whole request body.
 * When the next filter Y will receives the buffered request body and this callback will return
 * true.
 *
 * @param filter_envoy_ptr is the pointer to the DynamicModuleHttpFilter object of the
 * corresponding HTTP filter.
 * @return true if the latest received request body is the previously buffered request body, false
 * otherwise.
 */
bool envoy_dynamic_module_callback_http_received_buffered_request_body(
    envoy_dynamic_module_type_http_filter_envoy_ptr filter_envoy_ptr);

/**
 * envoy_dynamic_module_callback_http_received_buffered_response_body is called by the module to
 * check if the latest received response body actually is the previously buffered response body.
 *
 * For example, the previous filter X have stopped the filter chain and buffered the response body.
 * Then X resumes the filter chain after receiving the whole response body.
 * When the next filter Y will receives the buffered response body and this callback will return
 * true.
 *
 * @param filter_envoy_ptr is the pointer to the DynamicModuleHttpFilter object of the
 * corresponding HTTP filter.
 * @return true if the latest received response body is the previously buffered response body, false
 * otherwise.
 */
bool envoy_dynamic_module_callback_http_received_buffered_response_body(
    envoy_dynamic_module_type_http_filter_envoy_ptr filter_envoy_ptr);

// ---------------------------- Metadata Callbacks -----------------------------

/**
 * envoy_dynamic_module_callback_http_set_dynamic_metadata_number is called by the module to set
 * the number value of the dynamic metadata with the given namespace and key. If the metadata is
 * existing, it will be overwritten.
 *
 * @param filter_envoy_ptr is the pointer to the DynamicModuleHttpFilter object of the
 * corresponding HTTP filter.
 * @param ns is the namespace of the dynamic metadata.
 * @param key is the key of the dynamic metadata.
 * @param value is the number value of the dynamic metadata to be set.
 */
void envoy_dynamic_module_callback_http_set_dynamic_metadata_number(
    envoy_dynamic_module_type_http_filter_envoy_ptr filter_envoy_ptr,
    envoy_dynamic_module_type_module_buffer ns, envoy_dynamic_module_type_module_buffer key,
    double value);

/**
 * envoy_dynamic_module_callback_http_get_metadata_number is called by the module to get
 * the number value of the dynamic metadata with the given namespace and key. If the metadata is not
 * accessible, the namespace does not exist, the key does not exist or the value is not a number,
 * this returns false.
 *
 * @param filter_envoy_ptr is the pointer to the DynamicModuleHttpFilter object of the
 * corresponding HTTP filter.
 * @param metadata_source is the source of the metadata.
 * @param ns is the namespace of the dynamic metadata.
 * @param key is the key of the dynamic metadata.
 * @param result is the pointer to the variable where the number value of the dynamic metadata will
 * be stored.
 * @return true if the operation is successful, false otherwise.
 */
bool envoy_dynamic_module_callback_http_get_metadata_number(
    envoy_dynamic_module_type_http_filter_envoy_ptr filter_envoy_ptr,
    envoy_dynamic_module_type_metadata_source metadata_source,
    envoy_dynamic_module_type_module_buffer ns, envoy_dynamic_module_type_module_buffer key,
    double* result);

/**
 * envoy_dynamic_module_callback_http_set_dynamic_metadata_string is called by the module to set
 * the string value of the dynamic metadata with the given namespace and key. If the metadata is
 * existing, it will be overwritten.
 *
 * @param filter_envoy_ptr is the pointer to the DynamicModuleHttpFilter object of the
 * corresponding HTTP filter.
 * @param ns is the namespace of the dynamic metadata.
 * @param key is the key of the dynamic metadata.
 * @param value is the string value of the dynamic metadata to be set.
 */
void envoy_dynamic_module_callback_http_set_dynamic_metadata_string(
    envoy_dynamic_module_type_http_filter_envoy_ptr filter_envoy_ptr,
    envoy_dynamic_module_type_module_buffer ns, envoy_dynamic_module_type_module_buffer key,
    envoy_dynamic_module_type_module_buffer value);

/**
 * envoy_dynamic_module_callback_http_get_metadata_string is called by the module to get
 * the string value of the dynamic metadata with the given namespace and key. If the metadata is not
 * accessible, the namespace does not exist, the key does not exist or the value is not a string,
 * this returns false.
 *
 * @param filter_envoy_ptr is the pointer to the DynamicModuleHttpFilter object of the
 * corresponding HTTP filter.
 * @param metadata_source is the source of the metadata.
 * @param ns is the namespace of the dynamic metadata.
 * @param key is the key of the dynamic metadata.
 * @param result is the pointer to the pointer variable where the pointer to the buffer
 * of the string value will be stored.
 * @return true if the operation is successful, false otherwise.
 *
 * Note that the buffer pointed by the pointer stored in result is owned by Envoy, and
 * they are guaranteed to be valid until the end of the current event hook unless the setter
 * callback is called.
 */
bool envoy_dynamic_module_callback_http_get_metadata_string(
    envoy_dynamic_module_type_http_filter_envoy_ptr filter_envoy_ptr,
    envoy_dynamic_module_type_metadata_source metadata_source,
    envoy_dynamic_module_type_module_buffer ns, envoy_dynamic_module_type_module_buffer key,
    envoy_dynamic_module_type_envoy_buffer* result);

/**
 * envoy_dynamic_module_callback_http_set_dynamic_metadata_bool is called by the module to set
 * the bool value of the dynamic metadata with the given namespace and key. If the metadata is
 * existing, it will be overwritten.
 *
 * @param filter_envoy_ptr is the pointer to the DynamicModuleHttpFilter object of the
 * corresponding HTTP filter.
 * @param ns is the namespace of the dynamic metadata.
 * @param key is the key of the dynamic metadata.
 * @param value is the bool value of the dynamic metadata to be set.
 */
void envoy_dynamic_module_callback_http_set_dynamic_metadata_bool(
    envoy_dynamic_module_type_http_filter_envoy_ptr filter_envoy_ptr,
    envoy_dynamic_module_type_module_buffer ns, envoy_dynamic_module_type_module_buffer key,
    bool value);

/**
 * envoy_dynamic_module_callback_http_get_metadata_bool is called by the module to get
 * the bool value of the dynamic metadata with the given namespace and key. If the metadata is not
 * accessible, the namespace does not exist, the key does not exist or the value is not a bool,
 * this returns false.
 *
 * @param filter_envoy_ptr is the pointer to the DynamicModuleHttpFilter object of the
 * corresponding HTTP filter.
 * @param metadata_source is the source of the metadata.
 * @param ns is the namespace of the dynamic metadata.
 * @param key is the key of the dynamic metadata.
 * @param result is the pointer to the variable where the bool value of the dynamic metadata will
 * be stored.
 * @return true if the operation is successful, false otherwise.
 */
bool envoy_dynamic_module_callback_http_get_metadata_bool(
    envoy_dynamic_module_type_http_filter_envoy_ptr filter_envoy_ptr,
    envoy_dynamic_module_type_metadata_source metadata_source,
    envoy_dynamic_module_type_module_buffer ns, envoy_dynamic_module_type_module_buffer key,
    bool* result);

/**
 * envoy_dynamic_module_callback_http_get_metadata_keys_count is called by the module to get the
 * number of keys in the metadata namespace. If the metadata is not accessible or the namespace
 * does not exist, this returns 0.
 *
 * @param filter_envoy_ptr is the pointer to the DynamicModuleHttpFilter object of the
 * corresponding HTTP filter.
 * @param metadata_source is the source of the metadata.
 * @param ns is the namespace of the dynamic metadata.
 * @return the number of keys in the metadata namespace.
 */
size_t envoy_dynamic_module_callback_http_get_metadata_keys_count(
    envoy_dynamic_module_type_http_filter_envoy_ptr filter_envoy_ptr,
    envoy_dynamic_module_type_metadata_source metadata_source,
    envoy_dynamic_module_type_module_buffer ns);

/**
 * envoy_dynamic_module_callback_http_get_metadata_keys is called by the module to get all keys
 * in the metadata namespace. The keys are returned as an array of
 * envoy_dynamic_module_type_envoy_buffer.
 *
 * PRECONDITION: The module must ensure that the result_buffer_vector is valid and has enough length
 * to store all the keys. The module can use
 * envoy_dynamic_module_callback_http_get_metadata_keys_count to get the number of
 * keys before calling this function.
 *
 * @param filter_envoy_ptr is the pointer to the DynamicModuleHttpFilter object of the
 * corresponding HTTP filter.
 * @param metadata_source is the source of the metadata.
 * @param ns is the namespace of the dynamic metadata.
 * @param result_buffer_vector is the pointer to the array of envoy_dynamic_module_type_envoy_buffer
 * where the key strings will be stored. The lifetime of the buffer is guaranteed until the
 * end of the current event hook unless the setter callback is called.
 * @return true if the operation is successful, false otherwise.
 */
bool envoy_dynamic_module_callback_http_get_metadata_keys(
    envoy_dynamic_module_type_http_filter_envoy_ptr filter_envoy_ptr,
    envoy_dynamic_module_type_metadata_source metadata_source,
    envoy_dynamic_module_type_module_buffer ns,
    envoy_dynamic_module_type_envoy_buffer* result_buffer_vector);

/**
 * envoy_dynamic_module_callback_http_get_metadata_namespaces_count is called by the module to get
 * the number of namespaces in the metadata. If the metadata is not accessible, this returns 0.
 *
 * @param filter_envoy_ptr is the pointer to the DynamicModuleHttpFilter object of the
 * corresponding HTTP filter.
 * @param metadata_source is the source of the metadata.
 * @return the number of namespaces in the metadata.
 */
size_t envoy_dynamic_module_callback_http_get_metadata_namespaces_count(
    envoy_dynamic_module_type_http_filter_envoy_ptr filter_envoy_ptr,
    envoy_dynamic_module_type_metadata_source metadata_source);

/**
 * envoy_dynamic_module_callback_http_get_metadata_namespaces is called by the module to get all
 * namespace names in the metadata. The namespaces are returned as an array of
 * envoy_dynamic_module_type_envoy_buffer.
 *
 * PRECONDITION: The module must ensure that the result_buffer_vector is valid and has enough length
 * to store all the namespaces. The module can use
 * envoy_dynamic_module_callback_http_get_metadata_namespaces_count to get the number of
 * namespaces before calling this function.
 *
 * @param filter_envoy_ptr is the pointer to the DynamicModuleHttpFilter object of the
 * corresponding HTTP filter.
 * @param metadata_source is the source of the metadata.
 * @param result_buffer_vector is the pointer to the array of envoy_dynamic_module_type_envoy_buffer
 * where the namespace strings will be stored. The lifetime of the buffer is guaranteed until the
 * end of the current event hook unless the setter callback is called.
 * @return true if the operation is successful, false otherwise.
 */
bool envoy_dynamic_module_callback_http_get_metadata_namespaces(
    envoy_dynamic_module_type_http_filter_envoy_ptr filter_envoy_ptr,
    envoy_dynamic_module_type_metadata_source metadata_source,
    envoy_dynamic_module_type_envoy_buffer* result_buffer_vector);

/**
 * envoy_dynamic_module_callback_http_add_dynamic_metadata_list_number is called by the module to
 * append a number value to the dynamic metadata list stored under the given namespace and key. If
 * the key does not exist, a new list is created. If the key exists but is not a list, this returns
 * false. If the metadata is not accessible, this returns false.
 *
 * @param filter_envoy_ptr is the pointer to the DynamicModuleHttpFilter object of the
 * corresponding HTTP filter.
 * @param ns is the namespace of the dynamic metadata.
 * @param key is the key of the dynamic metadata whose value is expected to be a list.
 * @param value is the number value to append to the list.
 * @return true if the operation is successful, false otherwise.
 */
bool envoy_dynamic_module_callback_http_add_dynamic_metadata_list_number(
    envoy_dynamic_module_type_http_filter_envoy_ptr filter_envoy_ptr,
    envoy_dynamic_module_type_module_buffer ns, envoy_dynamic_module_type_module_buffer key,
    double value);

/**
 * envoy_dynamic_module_callback_http_add_dynamic_metadata_list_string is called by the module to
 * append a string value to the dynamic metadata list stored under the given namespace and key. If
 * the key does not exist, a new list is created. If the key exists but is not a list, this returns
 * false. If the metadata is not accessible, this returns false.
 *
 * @param filter_envoy_ptr is the pointer to the DynamicModuleHttpFilter object of the
 * corresponding HTTP filter.
 * @param ns is the namespace of the dynamic metadata.
 * @param key is the key of the dynamic metadata whose value is expected to be a list.
 * @param value is the string value to append to the list.
 * @return true if the operation is successful, false otherwise.
 */
bool envoy_dynamic_module_callback_http_add_dynamic_metadata_list_string(
    envoy_dynamic_module_type_http_filter_envoy_ptr filter_envoy_ptr,
    envoy_dynamic_module_type_module_buffer ns, envoy_dynamic_module_type_module_buffer key,
    envoy_dynamic_module_type_module_buffer value);

/**
 * envoy_dynamic_module_callback_http_add_dynamic_metadata_list_bool is called by the module to
 * append a bool value to the dynamic metadata list stored under the given namespace and key. If the
 * key does not exist, a new list is created. If the key exists but is not a list, this returns
 * false. If the metadata is not accessible, this returns false.
 *
 * @param filter_envoy_ptr is the pointer to the DynamicModuleHttpFilter object of the
 * corresponding HTTP filter.
 * @param ns is the namespace of the dynamic metadata.
 * @param key is the key of the dynamic metadata whose value is expected to be a list.
 * @param value is the bool value to append to the list.
 * @return true if the operation is successful, false otherwise.
 */
bool envoy_dynamic_module_callback_http_add_dynamic_metadata_list_bool(
    envoy_dynamic_module_type_http_filter_envoy_ptr filter_envoy_ptr,
    envoy_dynamic_module_type_module_buffer ns, envoy_dynamic_module_type_module_buffer key,
    bool value);

/**
 * envoy_dynamic_module_callback_http_get_metadata_list_size is called by the module to get the
 * number of elements in the metadata list stored under the given namespace and key. If the metadata
 * is not accessible, the namespace does not exist, the key does not exist, or the value is not a
 * list, this returns false.
 *
 * @param filter_envoy_ptr is the pointer to the DynamicModuleHttpFilter object of the
 * corresponding HTTP filter.
 * @param metadata_source is the source of the metadata.
 * @param ns is the namespace of the dynamic metadata.
 * @param key is the key of the dynamic metadata whose value is expected to be a list.
 * @param result is the pointer to the variable where the number of elements will be stored.
 * @return true if the operation is successful, false otherwise.
 */
bool envoy_dynamic_module_callback_http_get_metadata_list_size(
    envoy_dynamic_module_type_http_filter_envoy_ptr filter_envoy_ptr,
    envoy_dynamic_module_type_metadata_source metadata_source,
    envoy_dynamic_module_type_module_buffer ns, envoy_dynamic_module_type_module_buffer key,
    size_t* result);

/**
 * envoy_dynamic_module_callback_http_get_metadata_list_number is called by the module to get the
 * number value at the given index in the metadata list stored under the given namespace and key. If
 * the metadata is not accessible, the namespace does not exist, the key does not exist, the value
 * is not a list, the index is out of range, or the element at index is not a number, this returns
 * false.
 *
 * @param filter_envoy_ptr is the pointer to the DynamicModuleHttpFilter object of the
 * corresponding HTTP filter.
 * @param metadata_source is the source of the metadata.
 * @param ns is the namespace of the dynamic metadata.
 * @param key is the key of the dynamic metadata whose value is expected to be a list.
 * @param index is the zero-based index of the element to retrieve.
 * @param result is the pointer to the variable where the number value will be stored.
 * @return true if the operation is successful, false otherwise.
 */
bool envoy_dynamic_module_callback_http_get_metadata_list_number(
    envoy_dynamic_module_type_http_filter_envoy_ptr filter_envoy_ptr,
    envoy_dynamic_module_type_metadata_source metadata_source,
    envoy_dynamic_module_type_module_buffer ns, envoy_dynamic_module_type_module_buffer key,
    size_t index, double* result);

/**
 * envoy_dynamic_module_callback_http_get_metadata_list_string is called by the module to get the
 * string value at the given index in the metadata list stored under the given namespace and key. If
 * the metadata is not accessible, the namespace does not exist, the key does not exist, the value
 * is not a list, the index is out of range, or the element at index is not a string, this returns
 * false.
 *
 * @param filter_envoy_ptr is the pointer to the DynamicModuleHttpFilter object of the
 * corresponding HTTP filter.
 * @param metadata_source is the source of the metadata.
 * @param ns is the namespace of the dynamic metadata.
 * @param key is the key of the dynamic metadata whose value is expected to be a list.
 * @param index is the zero-based index of the element to retrieve.
 * @param result is the pointer to the envoy_buffer where the string value will be stored. The
 * lifetime of the buffer is guaranteed until the end of the current event hook unless the setter
 * callback is called.
 * @return true if the operation is successful, false otherwise.
 */
bool envoy_dynamic_module_callback_http_get_metadata_list_string(
    envoy_dynamic_module_type_http_filter_envoy_ptr filter_envoy_ptr,
    envoy_dynamic_module_type_metadata_source metadata_source,
    envoy_dynamic_module_type_module_buffer ns, envoy_dynamic_module_type_module_buffer key,
    size_t index, envoy_dynamic_module_type_envoy_buffer* result);

/**
 * envoy_dynamic_module_callback_http_get_metadata_list_bool is called by the module to get the
 * bool value at the given index in the metadata list stored under the given namespace and key. If
 * the metadata is not accessible, the namespace does not exist, the key does not exist, the value
 * is not a list, the index is out of range, or the element at index is not a bool, this returns
 * false.
 *
 * @param filter_envoy_ptr is the pointer to the DynamicModuleHttpFilter object of the
 * corresponding HTTP filter.
 * @param metadata_source is the source of the metadata.
 * @param ns is the namespace of the dynamic metadata.
 * @param key is the key of the dynamic metadata whose value is expected to be a list.
 * @param index is the zero-based index of the element to retrieve.
 * @param result is the pointer to the variable where the bool value will be stored.
 * @return true if the operation is successful, false otherwise.
 */
bool envoy_dynamic_module_callback_http_get_metadata_list_bool(
    envoy_dynamic_module_type_http_filter_envoy_ptr filter_envoy_ptr,
    envoy_dynamic_module_type_metadata_source metadata_source,
    envoy_dynamic_module_type_module_buffer ns, envoy_dynamic_module_type_module_buffer key,
    size_t index, bool* result);

// -------------------------- Filter State Callbacks ---------------------------

/**
 * envoy_dynamic_module_callback_http_set_filter_state_bytes is called by the module to set the
 * bytes value of the filter state with the given key. If the filter state is not accessible, this
 * returns false. If the key does not exist, it will be created.
 *
 * @param filter_envoy_ptr is the pointer to the DynamicModuleHttpFilter object of the
 * corresponding HTTP filter.
 * @param key is the key of the filter state.
 * @param value is the bytes value of the filter state to be set.
 * @return true if the operation is successful, false otherwise. Different from setting metadata,
 * this could fail if the same key already exists and be marked as read-only.
 */
bool envoy_dynamic_module_callback_http_set_filter_state_bytes(
    envoy_dynamic_module_type_http_filter_envoy_ptr filter_envoy_ptr,
    envoy_dynamic_module_type_module_buffer key, envoy_dynamic_module_type_module_buffer value);

/**
 * envoy_dynamic_module_callback_http_get_filter_state_bytes is called by the module to get the
 * bytes value of the filter state with the given key. If the filter state is not accessible, the
 * key does not exist or the value is not bytes, this returns false.
 *
 * @param filter_envoy_ptr is the pointer to the DynamicModuleHttpFilter object of the
 * corresponding HTTP filter.
 * @param key is the key of the filter state.
 * @param result is the pointer to the pointer variable where the pointer to the buffer
 * of the bytes value will be stored.
 * @return true if the operation is successful, false otherwise.
 *
 * Note that the buffer pointed by the pointer stored in result is owned by Envoy, and
 * they are guaranteed to be valid until the end of the current event hook unless the setter
 * callback is called.
 */
bool envoy_dynamic_module_callback_http_get_filter_state_bytes(
    envoy_dynamic_module_type_http_filter_envoy_ptr filter_envoy_ptr,
    envoy_dynamic_module_type_module_buffer key, envoy_dynamic_module_type_envoy_buffer* result);

/**
 * envoy_dynamic_module_callback_http_set_filter_state_typed is called by the module to set the
 * typed filter state with the given key. Unlike set_filter_state_bytes which stores a raw
 * StringAccessor, this uses the registered ObjectFactory for the key to create a properly typed
 * filter state object via createFromBytes. This is required for interoperability with built-in
 * Envoy filters that read filter state as typed objects (e.g., tcp_proxy reads
 * PerConnectionCluster).
 *
 * @param filter_envoy_ptr is the pointer to the DynamicModuleHttpFilter object of the
 * corresponding HTTP filter.
 * @param key is the key of the filter state. This must match a registered ObjectFactory name.
 * @param value is the serialized bytes value used to construct the typed object.
 * @return true if the operation is successful, false if the stream info is not available, no
 * ObjectFactory is registered for the key, the factory fails to create the object, or the key
 * already exists and is marked as read-only.
 */
bool envoy_dynamic_module_callback_http_set_filter_state_typed(
    envoy_dynamic_module_type_http_filter_envoy_ptr filter_envoy_ptr,
    envoy_dynamic_module_type_module_buffer key, envoy_dynamic_module_type_module_buffer value);

/**
 * envoy_dynamic_module_callback_http_get_filter_state_typed is called by the module to get the
 * serialized bytes value of a typed filter state object with the given key. This retrieves the
 * object generically and calls serializeAsString to get the bytes representation. This works with
 * any filter state object type, not just StringAccessor.
 *
 * @param filter_envoy_ptr is the pointer to the DynamicModuleHttpFilter object of the
 * corresponding HTTP filter.
 * @param key is the key of the filter state.
 * @param result is the pointer to the buffer where the serialized value will be stored.
 * @return true if the operation is successful, false if the stream info is not available, the key
 * does not exist, or the object does not support serialization.
 *
 * Note that the buffer pointed by the pointer stored in result is owned by Envoy, and
 * they are guaranteed to be valid until the end of the current event hook unless the setter
 * callback is called.
 */
bool envoy_dynamic_module_callback_http_get_filter_state_typed(
    envoy_dynamic_module_type_http_filter_envoy_ptr filter_envoy_ptr,
    envoy_dynamic_module_type_module_buffer key, envoy_dynamic_module_type_envoy_buffer* result);

// ---------------------- Other HTTP filter callbacks ----------------------------

/**
 * envoy_dynamic_module_callback_http_add_custom_flag is called by the module to add a custom flag
 * to indicate a noteworthy event of this stream. Multiple flags could be added and will be
 * concatenated with comma. It should not contain any empty or space characters (' ', '\t', '\f',
 * '\v', '\n', '\r'). to the HTTP stream. The flag can later be used in logging or metrics.
 *
 * @param filter_envoy_ptr is the pointer to the DynamicModuleHttpFilter object of the
 * corresponding HTTP filter.
 * @param flag is the custom flag to be added. The flag should not contain any empty or space
 * characters (' ', '\t', '\f', '\v', '\n', '\r') and should be very short to indicate a noteworthy
 * event of this stream.
 */
void envoy_dynamic_module_callback_http_add_custom_flag(
    envoy_dynamic_module_type_http_filter_envoy_ptr filter_envoy_ptr,
    envoy_dynamic_module_type_module_buffer flag);

// ---------------------- HTTP filter scheduler callbacks ------------------------

/**
 * envoy_dynamic_module_callback_http_filter_scheduler_new is called by the module to create a new
 * HTTP filter scheduler. The scheduler is used to dispatch HTTP filter operations from any thread
 * including the ones managed by the module.
 *
 * @param filter_envoy_ptr is the pointer to the DynamicModuleHttpFilter object of the
 * corresponding HTTP filter.
 * @return envoy_dynamic_module_type_http_filter_scheduler_module_ptr is the pointer to the
 * created HTTP filter scheduler.
 *
 * NOTE: it is caller's responsibility to delete the scheduler using
 * envoy_dynamic_module_callback_http_filter_scheduler_delete when it is no longer needed.
 * See the comment on envoy_dynamic_module_type_http_filter_scheduler_module_ptr.
 */
envoy_dynamic_module_type_http_filter_scheduler_module_ptr
envoy_dynamic_module_callback_http_filter_scheduler_new(
    envoy_dynamic_module_type_http_filter_envoy_ptr filter_envoy_ptr);

/**
 * envoy_dynamic_module_callback_http_filter_scheduler_commit is called by the module to
 * schedule a generic event to the HTTP filter on the worker thread it is running on.
 *
 * This will eventually end up invoking envoy_dynamic_module_on_http_filter_scheduled
 * event hook on the worker thread.
 *
 * This can be called multiple times to schedule multiple events to the same filter.
 *
 * @param scheduler_module_ptr is the pointer to the HTTP filter scheduler created by
 * envoy_dynamic_module_callback_http_filter_scheduler_new.
 * @param event_id is the ID of the event. This can be used to differentiate between multiple
 * events scheduled to the same filter. It can be any module-defined value.
 */
void envoy_dynamic_module_callback_http_filter_scheduler_commit(
    envoy_dynamic_module_type_http_filter_scheduler_module_ptr scheduler_module_ptr,
    uint64_t event_id);

/**
 * envoy_dynamic_module_callback_http_filter_scheduler_delete is called by the module to delete
 * the HTTP filter scheduler created by envoy_dynamic_module_callback_http_filter_scheduler_new.
 *
 * @param scheduler_module_ptr is the pointer to the HTTP filter scheduler created by
 * envoy_dynamic_module_callback_http_filter_scheduler_new.
 */
void envoy_dynamic_module_callback_http_filter_scheduler_delete(
    envoy_dynamic_module_type_http_filter_scheduler_module_ptr scheduler_module_ptr);

/**
 * envoy_dynamic_module_callback_http_filter_config_scheduler_new is called by the module to create
 * a new HTTP filter configuration scheduler. The scheduler is used to dispatch HTTP filter
 * configuration operations to the main thread from any thread including the ones managed by the
 * module.
 *
 * @param filter_config_envoy_ptr is the pointer to the DynamicModuleHttpFilterConfig object.
 * @return envoy_dynamic_module_type_http_filter_config_scheduler_module_ptr is the pointer to the
 * created HTTP filter configuration scheduler.
 *
 * NOTE: it is caller's responsibility to delete the scheduler using
 * envoy_dynamic_module_callback_http_filter_config_scheduler_delete when it is no longer needed.
 * See the comment on envoy_dynamic_module_type_http_filter_config_scheduler_module_ptr.
 */
envoy_dynamic_module_type_http_filter_config_scheduler_module_ptr
envoy_dynamic_module_callback_http_filter_config_scheduler_new(
    envoy_dynamic_module_type_http_filter_config_envoy_ptr filter_config_envoy_ptr);

/**
 * envoy_dynamic_module_callback_http_filter_config_scheduler_delete is called by the module to
 * delete the HTTP filter configuration scheduler created by
 * envoy_dynamic_module_callback_http_filter_config_scheduler_new.
 *
 * @param scheduler_module_ptr is the pointer to the HTTP filter configuration scheduler created by
 * envoy_dynamic_module_callback_http_filter_config_scheduler_new.
 */
void envoy_dynamic_module_callback_http_filter_config_scheduler_delete(
    envoy_dynamic_module_type_http_filter_config_scheduler_module_ptr scheduler_module_ptr);

/**
 * envoy_dynamic_module_callback_http_filter_config_scheduler_commit is called by the module to
 * schedule a generic event to the HTTP filter configuration on the main thread.
 *
 * This will eventually end up invoking envoy_dynamic_module_on_http_filter_config_scheduled
 * event hook on the main thread.
 *
 * This can be called multiple times to schedule multiple events to the same filter configuration.
 *
 * @param scheduler_module_ptr is the pointer to the HTTP filter configuration scheduler created by
 * envoy_dynamic_module_callback_http_filter_config_scheduler_new.
 * @param event_id is the ID of the event. This can be used to differentiate between multiple
 * events scheduled to the same filter configuration. It can be any module-defined value.
 */
void envoy_dynamic_module_callback_http_filter_config_scheduler_commit(
    envoy_dynamic_module_type_http_filter_config_scheduler_module_ptr scheduler_module_ptr,
    uint64_t event_id);

// ------------------- Misc Callbacks for HTTP Filters -------------------------

/**
 * envoy_dynamic_module_callback_http_clear_route_cache is called by the module to clear the route
 * cache for the HTTP filter. This is useful when the module wants to make their own routing
 * decision. This will be a no-op when it's called in the wrong phase.
 */
void envoy_dynamic_module_callback_http_clear_route_cache(
    envoy_dynamic_module_type_http_filter_envoy_ptr filter_envoy_ptr);

/**
 * envoy_dynamic_module_callback_http_filter_get_attribute_string is called by the module to get
 * the string attribute value. If the attribute is not accessible or the
 * value is not a string, this returns false.
 *
 * @param filter_envoy_ptr is the pointer to the DynamicModuleHttpFilter object of the
 * corresponding HTTP filter.
 * @param attribute_id is the ID of the attribute.
 * @param result is the pointer to the pointer variable where the pointer to the buffer
 * of the string value will be stored.
 * @return true if the operation is successful, false otherwise.
 *
 * Note: currently, not all attributes are implemented.
 */
bool envoy_dynamic_module_callback_http_filter_get_attribute_string(
    envoy_dynamic_module_type_http_filter_envoy_ptr filter_envoy_ptr,
    envoy_dynamic_module_type_attribute_id attribute_id,
    envoy_dynamic_module_type_envoy_buffer* result);

/**
 * envoy_dynamic_module_callback_http_filter_get_attribute_int is called by the module to get
 * an integer attribute value. If the attribute is not accessible or the
 * value is not an integer, this returns false.
 *
 * @param filter_envoy_ptr is the pointer to the DynamicModuleHttpFilter object of the
 * corresponding HTTP filter.
 * @param attribute_id is the ID of the attribute.
 * @param result is the pointer to the variable where the integer value of the attribute will be
 * stored.
 * @return true if the operation is successful, false otherwise.
 *
 * Note: currently, not all attributes are implemented.
 */
bool envoy_dynamic_module_callback_http_filter_get_attribute_int(
    envoy_dynamic_module_type_http_filter_envoy_ptr filter_envoy_ptr,
    envoy_dynamic_module_type_attribute_id attribute_id, uint64_t* result);

/**
 * envoy_dynamic_module_callback_http_filter_get_attribute_bool is called by the module to get
 * a boolean attribute value. If the attribute is not accessible or the
 * value is not a boolean, this returns false.
 *
 * @param filter_envoy_ptr is the pointer to the DynamicModuleHttpFilter object of the
 * corresponding HTTP filter.
 * @param attribute_id is the ID of the attribute.
 * @param result is the pointer to the variable where the bool value of the attribute will be
 * stored.
 * @return true if the operation is successful, false otherwise.
 *
 * Note: currently, not all attributes are implemented.
 */
bool envoy_dynamic_module_callback_http_filter_get_attribute_bool(
    envoy_dynamic_module_type_http_filter_envoy_ptr filter_envoy_ptr,
    envoy_dynamic_module_type_attribute_id attribute_id, bool* result);

/**
 * envoy_dynamic_module_callback_http_filter_http_callout is called by the module to initiate
 * an HTTP callout. The callout is initiated by the HTTP filter and the response is received in
 * envoy_dynamic_module_on_http_filter_http_callout_done.
 *
 * @param filter_envoy_ptr is the pointer to the DynamicModuleHttpFilter object of the
 * corresponding HTTP filter.
 * @param callout_id_out is a pointer to a variable where the callout ID will be stored. This can be
 * arbitrary and is used to differentiate between multiple calls from the same filter.
 * @param cluster_name is the name of the cluster to which the callout is sent.
 * @param headers is the headers of the request. It must contain :method, :path and host headers.
 * @param headers_size is the size of the headers.
 * @param body is the body of the request.
 * @param timeout_milliseconds is the timeout for the callout in milliseconds.
 * @return envoy_dynamic_module_type_http_callout_init_result is the result of the callout.
 */
envoy_dynamic_module_type_http_callout_init_result
envoy_dynamic_module_callback_http_filter_http_callout(
    envoy_dynamic_module_type_http_filter_envoy_ptr filter_envoy_ptr, uint64_t* callout_id_out,
    envoy_dynamic_module_type_module_buffer cluster_name,
    envoy_dynamic_module_type_module_http_header* headers, size_t headers_size,
    envoy_dynamic_module_type_module_buffer body, uint64_t timeout_milliseconds);

/**
 * envoy_dynamic_module_callback_http_filter_start_http_stream is called by the module to start
 * a streamable HTTP callout to a specified cluster. Unlike the one-shot HTTP callout, this allows
 * the module to receive response headers, body chunks, and trailers through separate event hooks,
 * enabling true streaming behavior.
 *
 * The stream will trigger the following event hooks in order:
 * 1. envoy_dynamic_module_on_http_filter_http_stream_headers - when response headers arrive
 * 2. envoy_dynamic_module_on_http_filter_http_stream_data - for each body chunk (may be called
 *    multiple times or not at all)
 * 3. envoy_dynamic_module_on_http_filter_http_stream_trailers - when trailers arrive (optional)
 * 4. envoy_dynamic_module_on_http_filter_http_stream_complete - when stream completes successfully
 *    OR
 *    envoy_dynamic_module_on_http_filter_http_stream_reset - if stream fails
 *
 * @param filter_envoy_ptr is the pointer to the DynamicModuleHttpFilter object of the
 * corresponding HTTP filter.
 * @param stream_id_out is a pointer to a variable where the stream handle will be stored. The
 * module can use this handle to reset the stream via
 * envoy_dynamic_module_callback_http_filter_reset_http_stream.
 * @param cluster_name is the name of the cluster to which the stream is sent.
 * @param headers is the headers of the request. It must contain :method, :path and host headers.
 * @param headers_size is the size of the headers.
 * @param body is the body of the request.
 * @param end_stream is true if the request stream should be ended after sending headers and body.
 * If true and body_size > 0, the body will be sent with end_stream=true.
 * If true and body_size is 0, headers will be sent with end_stream=true.
 * If false, the module can send additional data or trailers using send_http_stream_data() or
 * send_http_stream_trailers().
 * @param timeout_milliseconds is the timeout for the stream in milliseconds. If 0, no timeout is
 * set.
 * @return envoy_dynamic_module_type_http_callout_init_result is the result of the stream
 * initialization.
 */
envoy_dynamic_module_type_http_callout_init_result
envoy_dynamic_module_callback_http_filter_start_http_stream(
    envoy_dynamic_module_type_http_filter_envoy_ptr filter_envoy_ptr, uint64_t* stream_id_out,
    envoy_dynamic_module_type_module_buffer cluster_name,
    envoy_dynamic_module_type_module_http_header* headers, size_t headers_size,
    envoy_dynamic_module_type_module_buffer body, bool end_stream, uint64_t timeout_milliseconds);

/**
 * envoy_dynamic_module_callback_http_filter_reset_http_stream is called by the module to reset
 * or cancel an ongoing streamable HTTP callout. This causes the stream to be terminated and the
 * envoy_dynamic_module_on_http_filter_http_stream_reset event hook to be called.
 *
 * This can be called at any point after the stream is started and before it completes. After
 * calling this function, the stream handle becomes invalid.
 *
 * @param filter_envoy_ptr is the pointer to the DynamicModuleHttpFilter object of the
 * corresponding HTTP filter.
 * @param stream_id is the handle to the HTTP stream to reset.
 */
void envoy_dynamic_module_callback_http_filter_reset_http_stream(
    envoy_dynamic_module_type_http_filter_envoy_ptr filter_envoy_ptr, uint64_t stream_id);

/**
 * envoy_dynamic_module_callback_http_stream_send_data is called by the module to send request
 * body data on an active streamable HTTP callout. This can be called multiple times to stream
 * the request body in chunks.
 *
 * This must be called after the stream is started and headers have been sent. It can be called
 * multiple times until end_stream is set to true.
 *
 * @param filter_envoy_ptr is the pointer to the DynamicModuleHttpFilter object of the
 * corresponding HTTP filter.
 * @param stream_id is the handle to the HTTP stream.
 * @param data is the body data to send.
 * @param end_stream is true if this is the last data (no trailers will follow).
 * @return true if the operation is successful, false otherwise.
 */
bool envoy_dynamic_module_callback_http_stream_send_data(
    envoy_dynamic_module_type_http_filter_envoy_ptr filter_envoy_ptr, uint64_t stream_id,
    envoy_dynamic_module_type_module_buffer data, bool end_stream);

/**
 * envoy_dynamic_module_callback_http_stream_send_trailers is called by the module to send
 * request trailers on an active streamable HTTP callout. This implicitly ends the stream.
 *
 * This must be called after the stream is started and all request data has been sent.
 * After calling this, no more data can be sent on the stream.
 *
 * @param filter_envoy_ptr is the pointer to the DynamicModuleHttpFilter object of the
 * corresponding HTTP filter.
 * @param stream_id is the handle to the HTTP stream.
 * @param trailers is the trailers to send.
 * @param trailers_size is the size of the trailers.
 * @return true if the operation is successful, false otherwise.
 */
bool envoy_dynamic_module_callback_http_stream_send_trailers(
    envoy_dynamic_module_type_http_filter_envoy_ptr filter_envoy_ptr, uint64_t stream_id,
    envoy_dynamic_module_type_module_http_header* trailers, size_t trailers_size);

/**
 * envoy_dynamic_module_callback_http_filter_config_http_callout is called by the module to
 * initiate an HTTP callout from an HTTP filter configuration context. Unlike the per-filter
 * callout, this callout is tied to the filter configuration lifetime and is not bound to any
 * specific HTTP request. The response is received in
 * envoy_dynamic_module_on_http_filter_config_http_callout_done.
 *
 * @param filter_config_envoy_ptr is the pointer to the DynamicModuleHttpFilterConfig object.
 * @param callout_id_out is a pointer to a variable where the callout ID will be stored. This can
 * be arbitrary and is used to differentiate between multiple calls from the same filter config.
 * @param cluster_name is the name of the cluster to which the callout is sent.
 * @param headers is the headers of the request. It must contain :method, :path and host headers.
 * @param headers_size is the size of the headers.
 * @param body is the body of the request.
 * @param timeout_milliseconds is the timeout for the callout in milliseconds.
 * @return envoy_dynamic_module_type_http_callout_init_result is the result of the callout.
 */
envoy_dynamic_module_type_http_callout_init_result
envoy_dynamic_module_callback_http_filter_config_http_callout(
    envoy_dynamic_module_type_http_filter_config_envoy_ptr filter_config_envoy_ptr,
    uint64_t* callout_id_out, envoy_dynamic_module_type_module_buffer cluster_name,
    envoy_dynamic_module_type_module_http_header* headers, size_t headers_size,
    envoy_dynamic_module_type_module_buffer body, uint64_t timeout_milliseconds);

/**
 * envoy_dynamic_module_callback_http_filter_config_start_http_stream is called by the module to
 * start a streamable HTTP callout from an HTTP filter configuration context. Unlike the one-shot
 * HTTP callout, this allows the module to receive response headers, body chunks, and trailers
 * through separate event hooks, enabling true streaming behavior.
 *
 * The stream will trigger the following event hooks in order:
 * 1. envoy_dynamic_module_on_http_filter_config_http_stream_headers
 * 2. envoy_dynamic_module_on_http_filter_config_http_stream_data (may be called multiple times)
 * 3. envoy_dynamic_module_on_http_filter_config_http_stream_trailers (optional)
 * 4. envoy_dynamic_module_on_http_filter_config_http_stream_complete
 *    OR
 *    envoy_dynamic_module_on_http_filter_config_http_stream_reset
 *
 * @param filter_config_envoy_ptr is the pointer to the DynamicModuleHttpFilterConfig object.
 * @param stream_id_out is a pointer to a variable where the stream handle will be stored. The
 * module can use this handle to reset the stream via
 * envoy_dynamic_module_callback_http_filter_config_reset_http_stream.
 * @param cluster_name is the name of the cluster to which the stream is sent.
 * @param headers is the headers of the request. It must contain :method, :path and host headers.
 * @param headers_size is the size of the headers.
 * @param body is the body of the request.
 * @param end_stream is true if the request stream should be ended after sending headers and body.
 * If true and body_size > 0, the body will be sent with end_stream=true.
 * If true and body_size is 0, headers will be sent with end_stream=true.
 * If false, the module can send additional data or trailers using
 * envoy_dynamic_module_callback_http_filter_config_stream_send_data() or
 * envoy_dynamic_module_callback_http_filter_config_stream_send_trailers().
 * @param timeout_milliseconds is the timeout for the stream in milliseconds. If 0, no timeout is
 * set.
 * @return envoy_dynamic_module_type_http_callout_init_result is the result of the stream
 * initialization.
 */
envoy_dynamic_module_type_http_callout_init_result
envoy_dynamic_module_callback_http_filter_config_start_http_stream(
    envoy_dynamic_module_type_http_filter_config_envoy_ptr filter_config_envoy_ptr,
    uint64_t* stream_id_out, envoy_dynamic_module_type_module_buffer cluster_name,
    envoy_dynamic_module_type_module_http_header* headers, size_t headers_size,
    envoy_dynamic_module_type_module_buffer body, bool end_stream, uint64_t timeout_milliseconds);

/**
 * envoy_dynamic_module_callback_http_filter_config_reset_http_stream is called by the module to
 * reset or cancel an ongoing streamable HTTP callout started from an HTTP filter configuration
 * context. This causes the stream to be terminated and the
 * envoy_dynamic_module_on_http_filter_config_http_stream_reset event hook to be called.
 *
 * This can be called at any point after the stream is started and before it completes. After
 * calling this function, the stream handle becomes invalid.
 *
 * @param filter_config_envoy_ptr is the pointer to the DynamicModuleHttpFilterConfig object.
 * @param stream_id is the handle to the HTTP stream to reset.
 */
void envoy_dynamic_module_callback_http_filter_config_reset_http_stream(
    envoy_dynamic_module_type_http_filter_config_envoy_ptr filter_config_envoy_ptr,
    uint64_t stream_id);

/**
 * envoy_dynamic_module_callback_http_filter_config_stream_send_data is called by the module to
 * send request body data on an active streamable HTTP callout started from an HTTP filter
 * configuration context. This can be called multiple times to stream the request body in chunks.
 *
 * This must be called after the stream is started and headers have been sent. It can be called
 * multiple times until end_stream is set to true.
 *
 * @param filter_config_envoy_ptr is the pointer to the DynamicModuleHttpFilterConfig object.
 * @param stream_id is the handle to the HTTP stream.
 * @param data is the body data to send.
 * @param end_stream is true if this is the last data (no trailers will follow).
 * @return true if the operation is successful, false otherwise.
 */
bool envoy_dynamic_module_callback_http_filter_config_stream_send_data(
    envoy_dynamic_module_type_http_filter_config_envoy_ptr filter_config_envoy_ptr,
    uint64_t stream_id, envoy_dynamic_module_type_module_buffer data, bool end_stream);

/**
 * envoy_dynamic_module_callback_http_filter_config_stream_send_trailers is called by the module to
 * send request trailers on an active streamable HTTP callout started from an HTTP filter
 * configuration context. This implicitly ends the stream.
 *
 * This must be called after the stream is started and all request data has been sent.
 * After calling this, no more data can be sent on the stream.
 *
 * @param filter_config_envoy_ptr is the pointer to the DynamicModuleHttpFilterConfig object.
 * @param stream_id is the handle to the HTTP stream.
 * @param trailers is the trailers to send.
 * @param trailers_size is the size of the trailers.
 * @return true if the operation is successful, false otherwise.
 */
bool envoy_dynamic_module_callback_http_filter_config_stream_send_trailers(
    envoy_dynamic_module_type_http_filter_config_envoy_ptr filter_config_envoy_ptr,
    uint64_t stream_id, envoy_dynamic_module_type_module_http_header* trailers,
    size_t trailers_size);

/**
 * envoy_dynamic_module_callback_http_filter_continue_decoding is called by the module to continue
 * decoding the HTTP request.
 *
 * @param filter_envoy_ptr is the pointer to the DynamicModuleHttpFilter object of the
 * corresponding HTTP filter.
 */
void envoy_dynamic_module_callback_http_filter_continue_decoding(
    envoy_dynamic_module_type_http_filter_envoy_ptr filter_envoy_ptr);

/**
 * envoy_dynamic_module_callback_http_filter_continue_encoding is called by the module to continue
 * encoding the HTTP response.
 *
 * @param filter_envoy_ptr is the pointer to the DynamicModuleHttpFilter object of the
 * corresponding HTTP filter.
 */
void envoy_dynamic_module_callback_http_filter_continue_encoding(
    envoy_dynamic_module_type_http_filter_envoy_ptr filter_envoy_ptr);

/**
 * envoy_dynamic_module_callback_get_most_specific_route_config may be called by an HTTP filter
 * to retrieve the most specific per-route filter (based on the route object hierarchy).
 *
 * @param filter_envoy_ptr is the pointer to the DynamicModuleHttpFilter object of the corresponding
 * HTTP filter.
 * @return null if no per-route config exist. Otherwise, a pointer to the per-route config is
 * returned.
 */
envoy_dynamic_module_type_http_filter_per_route_config_module_ptr
envoy_dynamic_module_callback_get_most_specific_route_config(
    envoy_dynamic_module_type_http_filter_envoy_ptr filter_envoy_ptr);

// ------------------- Http Filter Callbacks - Misc ---------------

/**
 * envoy_dynamic_module_callback_http_filter_get_worker_index is called by the module to get the
 * worker index assigned to the current HTTP filter. This can be used by the module to manage
 * worker-specific resources or perform worker-specific logic.
 * @param filter_envoy_ptr is the pointer to the DynamicModuleHttpFilter object of the
 * corresponding HTTP filter.
 * @return the worker index assigned to the current HTTP filter.
 */
uint32_t envoy_dynamic_module_callback_http_filter_get_worker_index(
    envoy_dynamic_module_type_http_filter_envoy_ptr filter_envoy_ptr);

// ---------------------- HTTP filter socket option callbacks --------------------

/**
 * envoy_dynamic_module_callback_http_set_socket_option_int sets an integer socket option with
 * the given level, name, and state.
 *
 * @param filter_envoy_ptr is the pointer to the DynamicModuleHttpFilter object.
 * @param level is the socket option level (e.g., SOL_SOCKET).
 * @param name is the socket option name (e.g., SO_KEEPALIVE).
 * @param state is the socket state at which this option should be applied. For downstream
 *        sockets, this is ignored since the socket is already connected.
 * @param direction specifies whether to apply to upstream or downstream socket.
 * @param value is the integer value for the socket option.
 * @return true if the operation is successful, false otherwise.
 */
bool envoy_dynamic_module_callback_http_set_socket_option_int(
    envoy_dynamic_module_type_http_filter_envoy_ptr filter_envoy_ptr, int64_t level, int64_t name,
    envoy_dynamic_module_type_socket_option_state state,
    envoy_dynamic_module_type_socket_direction direction, int64_t value);

/**
 * envoy_dynamic_module_callback_http_set_socket_option_bytes sets a bytes socket option with
 * the given level, name, and state.
 *
 * @param filter_envoy_ptr is the pointer to the DynamicModuleHttpFilter object.
 * @param level is the socket option level.
 * @param name is the socket option name.
 * @param state is the socket state at which this option should be applied. For downstream
 *        sockets, this is ignored since the socket is already connected.
 * @param direction specifies whether to apply to upstream or downstream socket.
 * @param value is the byte buffer value for the socket option.
 * @return true if the operation is successful, false otherwise.
 */
bool envoy_dynamic_module_callback_http_set_socket_option_bytes(
    envoy_dynamic_module_type_http_filter_envoy_ptr filter_envoy_ptr, int64_t level, int64_t name,
    envoy_dynamic_module_type_socket_option_state state,
    envoy_dynamic_module_type_socket_direction direction,
    envoy_dynamic_module_type_module_buffer value);

/**
 * envoy_dynamic_module_callback_http_get_socket_option_int retrieves an integer socket option
 * value.
 *
 * @param filter_envoy_ptr is the pointer to the DynamicModuleHttpFilter object.
 * @param level is the socket option level.
 * @param name is the socket option name.
 * @param state is the socket state.
 * @param direction specifies whether to get from upstream or downstream socket.
 * @param value_out is the pointer to store the retrieved integer value.
 * @return true if the option is found, false otherwise.
 */
bool envoy_dynamic_module_callback_http_get_socket_option_int(
    envoy_dynamic_module_type_http_filter_envoy_ptr filter_envoy_ptr, int64_t level, int64_t name,
    envoy_dynamic_module_type_socket_option_state state,
    envoy_dynamic_module_type_socket_direction direction, int64_t* value_out);

/**
 * envoy_dynamic_module_callback_http_get_socket_option_bytes retrieves a bytes socket option
 * value.
 *
 * @param filter_envoy_ptr is the pointer to the DynamicModuleHttpFilter object.
 * @param level is the socket option level.
 * @param name is the socket option name.
 * @param state is the socket state.
 * @param direction specifies whether to get from upstream or downstream socket.
 * @param value_out is the pointer to store the retrieved buffer. The buffer is owned by Envoy and
 * valid until the filter is destroyed.
 * @return true if the option is found, false otherwise.
 */
bool envoy_dynamic_module_callback_http_get_socket_option_bytes(
    envoy_dynamic_module_type_http_filter_envoy_ptr filter_envoy_ptr, int64_t level, int64_t name,
    envoy_dynamic_module_type_socket_option_state state,
    envoy_dynamic_module_type_socket_direction direction,
    envoy_dynamic_module_type_envoy_buffer* value_out);

// ------------------- HTTP filter buffer limit callbacks --------------------

/**
 * envoy_dynamic_module_callback_http_get_buffer_limit retrieves the current buffer limit for the
 * HTTP filter. This is the maximum amount of data that can be buffered for body data before
 * backpressure is applied. A buffer limit of 0 bytes indicates no limits are applied.
 *
 * @param filter_envoy_ptr is the pointer to the DynamicModuleHttpFilter object.
 * @return the current buffer limit in bytes.
 */
uint64_t envoy_dynamic_module_callback_http_get_buffer_limit(
    envoy_dynamic_module_type_http_filter_envoy_ptr filter_envoy_ptr);

/**
 * envoy_dynamic_module_callback_http_set_buffer_limit sets the buffer limit for the HTTP filter.
 * This controls the maximum amount of data that can be buffered for body data before backpressure
 * is applied.
 *
 * It is recommended (but not required) that filters calling this function should generally only
 * perform increases to the buffer limit, to avoid potentially conflicting with the buffer
 * requirements of other filters in the chain.
 *
 * @param filter_envoy_ptr is the pointer to the DynamicModuleHttpFilter object.
 * @param limit is the desired buffer limit in bytes.
 */
void envoy_dynamic_module_callback_http_set_buffer_limit(
    envoy_dynamic_module_type_http_filter_envoy_ptr filter_envoy_ptr, uint64_t limit);

// ----------------------------- Tracing callbacks -----------------------------

/**
 * envoy_dynamic_module_type_span_envoy_ptr is a raw pointer to the active Tracing::Span in Envoy.
 * This is the span associated with the current HTTP stream.
 *
 * OWNERSHIP: Envoy owns the pointer. The span is valid for the lifetime of the HTTP stream.
 * Modules must not call finish on the active span as Envoy manages its lifecycle.
 */
typedef void* envoy_dynamic_module_type_span_envoy_ptr;

/**
 * envoy_dynamic_module_type_child_span_module_ptr is a pointer to a child span created by the
 * module via envoy_dynamic_module_callback_http_span_spawn_child. Child spans are owned by the
 * module and must be finished by calling envoy_dynamic_module_callback_http_child_span_finish.
 *
 * OWNERSHIP: The module is responsible for managing the lifetime. The span must be finished
 * by calling envoy_dynamic_module_callback_http_child_span_finish when done.
 */
typedef void* envoy_dynamic_module_type_child_span_module_ptr;

/**
 * envoy_dynamic_module_callback_http_get_active_span retrieves the active tracing span for the
 * current HTTP stream. This span can be used to add tags, logs, or spawn child spans.
 *
 * @param filter_envoy_ptr is the pointer to the DynamicModuleHttpFilter object.
 * @return the pointer to the active span. Returns nullptr if tracing is not enabled
 *         or no span is available for this stream.
 */
envoy_dynamic_module_type_span_envoy_ptr envoy_dynamic_module_callback_http_get_active_span(
    envoy_dynamic_module_type_http_filter_envoy_ptr filter_envoy_ptr);

/**
 * envoy_dynamic_module_callback_http_span_set_tag sets a tag on the given span.
 * Tags are key-value pairs that provide metadata about the span.
 *
 * @param span is the pointer to the span (either active span or child span).
 * @param key is the tag key.
 * @param value is the tag value.
 */
void envoy_dynamic_module_callback_http_span_set_tag(envoy_dynamic_module_type_span_envoy_ptr span,
                                                     envoy_dynamic_module_type_module_buffer key,
                                                     envoy_dynamic_module_type_module_buffer value);

/**
 * envoy_dynamic_module_callback_http_span_set_operation sets the operation name on the given span.
 *
 * @param span is the pointer to the span (either active span or child span).
 * @param operation is the operation name to set.
 */
void envoy_dynamic_module_callback_http_span_set_operation(
    envoy_dynamic_module_type_span_envoy_ptr span,
    envoy_dynamic_module_type_module_buffer operation);

/**
 * envoy_dynamic_module_callback_http_span_log records an event on the given span.
 * The event is recorded with the current timestamp from the filter's dispatcher.
 *
 * @param filter_envoy_ptr is the pointer to the DynamicModuleHttpFilter object.
 * @param span is the pointer to the span (either active span or child span).
 * @param event is the event message to log.
 */
void envoy_dynamic_module_callback_http_span_log(
    envoy_dynamic_module_type_http_filter_envoy_ptr filter_envoy_ptr,
    envoy_dynamic_module_type_span_envoy_ptr span, envoy_dynamic_module_type_module_buffer event);

/**
 * envoy_dynamic_module_callback_http_span_set_sampled overrides the sampling decision for the span.
 * If sampled is false, this span and any subsequent child spans will not be reported
 * to the tracing system.
 *
 * @param span is the pointer to the span (either active span or child span).
 * @param sampled is true if the span should be sampled, false otherwise.
 */
void envoy_dynamic_module_callback_http_span_set_sampled(
    envoy_dynamic_module_type_span_envoy_ptr span, bool sampled);

/**
 * envoy_dynamic_module_callback_http_span_get_baggage retrieves a baggage value from the span.
 * Baggage data may have been set by this span or any parent spans.
 *
 * @param span is the pointer to the span (either active span or child span).
 * @param key is the baggage key to retrieve.
 * @param result is the pointer to store the baggage value. The buffer uses thread-local storage
 *        and is valid until the next tracing callback on the same thread. The module should copy
 *        the value if it needs to persist beyond immediate use.
 * @return true if the baggage key was found, false otherwise.
 */
bool envoy_dynamic_module_callback_http_span_get_baggage(
    envoy_dynamic_module_type_span_envoy_ptr span, envoy_dynamic_module_type_module_buffer key,
    envoy_dynamic_module_type_envoy_buffer* result);

/**
 * envoy_dynamic_module_callback_http_span_set_baggage sets a baggage value on the span.
 * All subsequent child spans will have access to this baggage.
 *
 * @param span is the pointer to the span (either active span or child span).
 * @param key is the baggage key.
 * @param value is the baggage value.
 */
void envoy_dynamic_module_callback_http_span_set_baggage(
    envoy_dynamic_module_type_span_envoy_ptr span, envoy_dynamic_module_type_module_buffer key,
    envoy_dynamic_module_type_module_buffer value);

/**
 * envoy_dynamic_module_callback_http_span_get_trace_id retrieves the trace ID from the span.
 * The trace ID may be generated for this span, propagated by parent spans, or not yet created.
 *
 * @param span is the pointer to the span (either active span or child span).
 * @param result is the pointer to store the trace ID. The buffer uses thread-local storage
 *        and is valid until the next tracing callback on the same thread. The module should copy
 *        the value if it needs to persist beyond immediate use.
 * @return true if the trace ID was retrieved successfully, false otherwise.
 */
bool envoy_dynamic_module_callback_http_span_get_trace_id(
    envoy_dynamic_module_type_span_envoy_ptr span, envoy_dynamic_module_type_envoy_buffer* result);

/**
 * envoy_dynamic_module_callback_http_span_get_span_id retrieves the span ID from the span.
 *
 * @param span is the pointer to the span (either active span or child span).
 * @param result is the pointer to store the span ID. The buffer uses thread-local storage
 *        and is valid until the next tracing callback on the same thread. The module should copy
 *        the value if it needs to persist beyond immediate use.
 * @return true if the span ID was retrieved successfully, false otherwise.
 */
bool envoy_dynamic_module_callback_http_span_get_span_id(
    envoy_dynamic_module_type_span_envoy_ptr span, envoy_dynamic_module_type_envoy_buffer* result);

/**
 * envoy_dynamic_module_callback_http_span_spawn_child creates a child span with the given
 * operation name. The child span is owned by the module and must be finished by calling
 * envoy_dynamic_module_callback_http_child_span_finish.
 *
 * @param filter_envoy_ptr is the pointer to the DynamicModuleHttpFilter object.
 * @param span is the pointer to the parent span (either active span or another child span).
 * @param operation_name is the operation name for the child span.
 * @return the pointer to the child span. Returns nullptr if the span could not be created.
 */
envoy_dynamic_module_type_child_span_module_ptr envoy_dynamic_module_callback_http_span_spawn_child(
    envoy_dynamic_module_type_http_filter_envoy_ptr filter_envoy_ptr,
    envoy_dynamic_module_type_span_envoy_ptr span,
    envoy_dynamic_module_type_module_buffer operation_name);

/**
 * envoy_dynamic_module_callback_http_child_span_finish finishes and releases a child span.
 * After calling this function, the span pointer becomes invalid and must not be used.
 *
 * @param span is the pointer to the child span to finish.
 */
void envoy_dynamic_module_callback_http_child_span_finish(
    envoy_dynamic_module_type_child_span_module_ptr span);

// ------------------- Cluster/Upstream Information Callbacks -------------------------

/**
 * envoy_dynamic_module_callback_http_get_cluster_name retrieves the name of the cluster that the
 * current request is routed to. This is useful for making routing decisions or for logging.
 *
 * @param filter_envoy_ptr is the pointer to the DynamicModuleHttpFilter object.
 * @param result is the pointer to store the cluster name. The buffer is owned by Envoy and is
 * valid until the end of the current event hook or until the route changes.
 * @return true if the cluster name was retrieved successfully, false otherwise (e.g., no route
 * selected yet).
 */
bool envoy_dynamic_module_callback_http_get_cluster_name(
    envoy_dynamic_module_type_http_filter_envoy_ptr filter_envoy_ptr,
    envoy_dynamic_module_type_envoy_buffer* result);

/**
 * envoy_dynamic_module_callback_http_get_cluster_host_count retrieves the host counts for the
 * cluster that the current request is routed to. This provides visibility into the cluster's
 * health state and can be used to implement scale-to-zero logic or custom load balancing decisions.
 *
 * @param filter_envoy_ptr is the pointer to the DynamicModuleHttpFilter object.
 * @param priority is the priority level to query (0 for default priority).
 * @param total_count is the pointer to store the total number of hosts. Can be null if not needed.
 * @param healthy_count is the pointer to store the number of healthy hosts. Can be null if not
 * needed.
 * @param degraded_count is the pointer to store the number of degraded hosts. Can be null if not
 * needed.
 * @return true if the counts were retrieved successfully, false otherwise (e.g., no cluster
 * available).
 */
bool envoy_dynamic_module_callback_http_get_cluster_host_count(
    envoy_dynamic_module_type_http_filter_envoy_ptr filter_envoy_ptr, uint32_t priority,
    size_t* total_count, size_t* healthy_count, size_t* degraded_count);

/**
 * envoy_dynamic_module_callback_http_set_upstream_override_host sets the override host to be used
 * by the upstream load balancer. If the target host exists in the host list of the routed cluster,
 * this host should be selected first. This is useful for implementing sticky sessions, host
 * affinity, or custom load balancing logic.
 *
 * @param filter_envoy_ptr is the pointer to the DynamicModuleHttpFilter object.
 * @param host is the host address to override (e.g., "10.0.0.1:8080"). Must be a valid IP address.
 * @param strict if true, the request will fail if the override host is not available. If false,
 * normal load balancing will be used as a fallback.
 * @return true if the override host was set successfully, false if the host address is invalid.
 */
bool envoy_dynamic_module_callback_http_set_upstream_override_host(
    envoy_dynamic_module_type_http_filter_envoy_ptr filter_envoy_ptr,
    envoy_dynamic_module_type_module_buffer host, bool strict);

// ------------------- Stream Control Callbacks -------------------------

/**
 * envoy_dynamic_module_callback_http_filter_reset_stream resets the HTTP stream with the specified
 * reason. This is useful for terminating the stream when an error condition is detected or when
 * the filter needs to abort processing.
 *
 * After calling this function, no further filter callbacks will be invoked for this stream except
 * for the destroy callback.
 *
 * @param filter_envoy_ptr is the pointer to the DynamicModuleHttpFilter object of the
 * corresponding HTTP filter.
 * @param reason is the reason for resetting the stream.
 * @param details is an optional details string explaining the reset reason. Can be empty.
 */
void envoy_dynamic_module_callback_http_filter_reset_stream(
    envoy_dynamic_module_type_http_filter_envoy_ptr filter_envoy_ptr,
    envoy_dynamic_module_type_http_filter_stream_reset_reason reason,
    envoy_dynamic_module_type_module_buffer details);

/**
 * envoy_dynamic_module_callback_http_filter_send_go_away_and_close sends a GOAWAY frame to the
 * downstream and closes the connection. This is useful for implementing graceful connection
 * shutdown scenarios.
 *
 * @param filter_envoy_ptr is the pointer to the DynamicModuleHttpFilter object of the
 * corresponding HTTP filter.
 * @param graceful if true, initiates a graceful drain sequence before closing. If false,
 * sends GOAWAY and closes immediately.
 */
void envoy_dynamic_module_callback_http_filter_send_go_away_and_close(
    envoy_dynamic_module_type_http_filter_envoy_ptr filter_envoy_ptr, bool graceful);

/**
 * envoy_dynamic_module_callback_http_filter_recreate_stream recreates the HTTP stream, optionally
 * with new headers. This is useful for implementing internal redirects or request retries.
 *
 * After calling this function successfully, the current filter chain will be destroyed and a new
 * stream will be created. The filter should return StopIteration from the current event hook.
 *
 * @param filter_envoy_ptr is the pointer to the DynamicModuleHttpFilter object of the
 * corresponding HTTP filter.
 * @param headers is an optional array of new headers to use for the recreated stream. If null,
 * the original headers will be reused.
 * @param headers_size is the size of the headers array.
 * @return true if the stream recreation was initiated successfully, false otherwise (e.g., if
 * the request body has not been fully received yet or if the stream cannot be recreated).
 */
bool envoy_dynamic_module_callback_http_filter_recreate_stream(
    envoy_dynamic_module_type_http_filter_envoy_ptr filter_envoy_ptr,
    envoy_dynamic_module_type_module_http_header* headers, size_t headers_size);

/**
 * envoy_dynamic_module_callback_http_clear_route_cluster_cache clears only the cluster selection
 * for the current route without clearing the entire route cache.
 *
 * This is a subset of envoy_dynamic_module_callback_http_clear_route_cache. Use this when a filter
 * modifies headers that affect cluster selection but not the route itself. This is more efficient
 * than clearing the entire route cache.
 *
 * @param filter_envoy_ptr is the pointer to the DynamicModuleHttpFilter object of the
 * corresponding HTTP filter.
 */
void envoy_dynamic_module_callback_http_clear_route_cluster_cache(
    envoy_dynamic_module_type_http_filter_envoy_ptr filter_envoy_ptr);

// =============================================================================
// ============================= Network Filter ================================
// =============================================================================

// =============================================================================
// Network Filter Types
// =============================================================================

/**
 * envoy_dynamic_module_type_network_filter_config_envoy_ptr is a raw pointer to
 * the DynamicModuleNetworkFilterConfig class in Envoy. This is passed to the module when
 * creating a new in-module network filter configuration and used to access the network
 * filter-scoped information.
 *
 * This has 1:1 correspondence with envoy_dynamic_module_type_network_filter_config_module_ptr in
 * the module.
 *
 * OWNERSHIP: Envoy owns the pointer.
 */
typedef void* envoy_dynamic_module_type_network_filter_config_envoy_ptr;

/**
 * envoy_dynamic_module_type_network_filter_config_module_ptr is a pointer to an in-module network
 * filter configuration corresponding to an Envoy network filter configuration. The config is
 * responsible for creating a new network filter that corresponds to each TCP connection.
 *
 * This has 1:1 correspondence with the DynamicModuleNetworkFilterConfig class in Envoy.
 *
 * OWNERSHIP: The module is responsible for managing the lifetime of the pointer. The pointer can be
 * released when envoy_dynamic_module_on_network_filter_config_destroy is called for the same
 * pointer.
 */
typedef const void* envoy_dynamic_module_type_network_filter_config_module_ptr;

/**
 * envoy_dynamic_module_type_network_filter_envoy_ptr is a raw pointer to the
 * DynamicModuleNetworkFilter class in Envoy. This is passed to the module when creating a new
 * network filter for each TCP connection and used to access the network filter-scoped information
 * such as connection data, buffers, etc.
 *
 * This has 1:1 correspondence with envoy_dynamic_module_type_network_filter_module_ptr in the
 * module.
 *
 * OWNERSHIP: Envoy owns the pointer, and can be accessed by the module until the filter is
 * destroyed, i.e. envoy_dynamic_module_on_network_filter_destroy is called.
 */
typedef void* envoy_dynamic_module_type_network_filter_envoy_ptr;

/**
 * envoy_dynamic_module_type_network_filter_module_ptr is a pointer to an in-module network filter
 * corresponding to an Envoy network filter. The filter is responsible for processing each TCP
 * connection.
 *
 * This has 1:1 correspondence with the DynamicModuleNetworkFilter class in Envoy.
 *
 * OWNERSHIP: The module is responsible for managing the lifetime of the pointer. The pointer can be
 * released when envoy_dynamic_module_on_network_filter_destroy is called for the same pointer.
 */
typedef const void* envoy_dynamic_module_type_network_filter_module_ptr;

/**
 * envoy_dynamic_module_type_network_filter_scheduler_module_ptr is a raw pointer to the
 * DynamicModuleNetworkFilterScheduler class in Envoy.
 *
 * OWNERSHIP: The allocation is done by Envoy but the module is responsible for managing the
 * lifetime of the pointer. Notably, it must be explicitly destroyed by the module
 * when scheduling the network filter event is done. The creation of this pointer is done by
 * envoy_dynamic_module_callback_network_filter_scheduler_new and the scheduling and destruction is
 * done by envoy_dynamic_module_callback_network_filter_scheduler_delete. Since its lifecycle is
 * owned/managed by the module, this has _module_ptr suffix.
 */
typedef void* envoy_dynamic_module_type_network_filter_scheduler_module_ptr;

/**
 * envoy_dynamic_module_type_network_filter_config_scheduler_module_ptr is a raw pointer to the
 * DynamicModuleNetworkFilterConfigScheduler class in Envoy.
 *
 * OWNERSHIP: The allocation is done by Envoy but the module is responsible for managing the
 * lifetime of the pointer. Notably, it must be explicitly destroyed by the module
 * when scheduling the network filter config event is done. The creation of this pointer is done by
 * envoy_dynamic_module_callback_network_filter_config_scheduler_new and the scheduling and
 * destruction is done by envoy_dynamic_module_callback_network_filter_config_scheduler_delete.
 * Since its lifecycle is owned/managed by the module, this has _module_ptr suffix.
 */
typedef void* envoy_dynamic_module_type_network_filter_config_scheduler_module_ptr;

/**
 * envoy_dynamic_module_type_on_network_filter_data_status represents the status of the filter
 * after processing data. This corresponds to `Network::FilterStatus` in envoy/network/filter.h.
 */
typedef enum envoy_dynamic_module_type_on_network_filter_data_status {
  // Continue to further filters.
  envoy_dynamic_module_type_on_network_filter_data_status_Continue,
  // Stop executing further filters.
  envoy_dynamic_module_type_on_network_filter_data_status_StopIteration,
} envoy_dynamic_module_type_on_network_filter_data_status;

/**
 * envoy_dynamic_module_type_network_connection_close_type represents how to close the connection.
 * This corresponds to `Network::ConnectionCloseType` in envoy/network/connection.h.
 */
typedef enum envoy_dynamic_module_type_network_connection_close_type {
  // Flush pending write data before raising ConnectionEvent::LocalClose.
  envoy_dynamic_module_type_network_connection_close_type_FlushWrite,
  // Do not flush any pending data. Write the pending data to the transport and then immediately
  // raise ConnectionEvent::LocalClose.
  envoy_dynamic_module_type_network_connection_close_type_NoFlush,
  // Flush pending write data and delay raising ConnectionEvent::LocalClose until the delayed_close
  // timeout has expired.
  envoy_dynamic_module_type_network_connection_close_type_FlushWriteAndDelay,
  // Do not write pending data and immediately raise ConnectionEvent::LocalClose.
  envoy_dynamic_module_type_network_connection_close_type_Abort,
  // Do not write pending data, immediately send RST, and immediately raise
  // ConnectionEvent::LocalClose.
  envoy_dynamic_module_type_network_connection_close_type_AbortReset,
} envoy_dynamic_module_type_network_connection_close_type;

/**
 * envoy_dynamic_module_type_network_connection_event represents connection events.
 * This corresponds to `Network::ConnectionEvent` in envoy/network/connection.h.
 */
typedef enum envoy_dynamic_module_type_network_connection_event {
  // Remote close.
  envoy_dynamic_module_type_network_connection_event_RemoteClose,
  // Local close.
  envoy_dynamic_module_type_network_connection_event_LocalClose,
  // Connected.
  envoy_dynamic_module_type_network_connection_event_Connected,
  // Connected with 0-RTT.
  envoy_dynamic_module_type_network_connection_event_ConnectedZeroRtt,
} envoy_dynamic_module_type_network_connection_event;

/**
 * envoy_dynamic_module_type_network_connection_state represents the current state of a connection.
 * This corresponds to `Network::Connection::State` in envoy/network/connection.h.
 */
typedef enum envoy_dynamic_module_type_network_connection_state {
  // Connection is open.
  envoy_dynamic_module_type_network_connection_state_Open,
  // Connection is closing.
  envoy_dynamic_module_type_network_connection_state_Closing,
  // Connection is closed.
  envoy_dynamic_module_type_network_connection_state_Closed,
} envoy_dynamic_module_type_network_connection_state;

/**
 * envoy_dynamic_module_type_network_read_disable_status represents the result of calling
 * read_disable on a connection.
 * This corresponds to `Network::Connection::ReadDisableStatus` in envoy/network/connection.h.
 */
typedef enum envoy_dynamic_module_type_network_read_disable_status {
  // No transition occurred.
  envoy_dynamic_module_type_network_read_disable_status_NoTransition,
  // Reading is still disabled.
  envoy_dynamic_module_type_network_read_disable_status_StillReadDisabled,
  // Transitioned from disabled to enabled.
  envoy_dynamic_module_type_network_read_disable_status_TransitionedToReadEnabled,
  // Transitioned from enabled to disabled.
  envoy_dynamic_module_type_network_read_disable_status_TransitionedToReadDisabled,
} envoy_dynamic_module_type_network_read_disable_status;

// =============================================================================
// Network Filter Event Hooks
// =============================================================================

/**
 * envoy_dynamic_module_on_network_filter_config_new is called by the main thread when the network
 * filter config is loaded. The function returns a
 * envoy_dynamic_module_type_network_filter_config_module_ptr for given name and config.
 *
 * @param filter_config_envoy_ptr is the pointer to the DynamicModuleNetworkFilterConfig object for
 * the corresponding config.
 * @param name is the name of the filter owned by Envoy.
 * @param config is the configuration for the module owned by Envoy.
 * @return envoy_dynamic_module_type_network_filter_config_module_ptr is the pointer to the
 * in-module network filter configuration. Returning nullptr indicates a failure to initialize the
 * module. When it fails, the filter configuration will be rejected.
 */
envoy_dynamic_module_type_network_filter_config_module_ptr
envoy_dynamic_module_on_network_filter_config_new(
    envoy_dynamic_module_type_network_filter_config_envoy_ptr filter_config_envoy_ptr,
    envoy_dynamic_module_type_envoy_buffer name, envoy_dynamic_module_type_envoy_buffer config);

/**
 * envoy_dynamic_module_on_network_filter_config_destroy is called when the network filter
 * configuration is destroyed in Envoy. The module should release any resources associated with
 * the corresponding in-module network filter configuration.
 *
 * @param filter_config_ptr is a pointer to the in-module network filter configuration whose
 * corresponding Envoy network filter configuration is being destroyed.
 */
void envoy_dynamic_module_on_network_filter_config_destroy(
    envoy_dynamic_module_type_network_filter_config_module_ptr filter_config_ptr);

/**
 * envoy_dynamic_module_on_network_filter_new is called when a new network filter is created for
 * each TCP connection.
 *
 * @param filter_config_ptr is the pointer to the in-module network filter configuration.
 * @param filter_envoy_ptr is the pointer to the DynamicModuleNetworkFilter object of the
 * corresponding network filter.
 * @return envoy_dynamic_module_type_network_filter_module_ptr is the pointer to the in-module
 * network filter. Returning nullptr indicates a failure to initialize the module. When it fails,
 * the connection will be closed.
 */
envoy_dynamic_module_type_network_filter_module_ptr envoy_dynamic_module_on_network_filter_new(
    envoy_dynamic_module_type_network_filter_config_module_ptr filter_config_ptr,
    envoy_dynamic_module_type_network_filter_envoy_ptr filter_envoy_ptr);

/**
 * envoy_dynamic_module_on_network_filter_new_connection is called when a new TCP connection is
 * established. This is called after the filter is created and callbacks are initialized.
 *
 * @param filter_envoy_ptr is the pointer to the DynamicModuleNetworkFilter object of the
 * corresponding network filter.
 * @param filter_module_ptr is the pointer to the in-module network filter created by
 * envoy_dynamic_module_on_network_filter_new.
 * @return envoy_dynamic_module_type_on_network_filter_data_status is the status of the filter.
 * Continue means further filters should be invoked, StopIteration means further filters should
 * not be invoked until continueReading() is called.
 */
envoy_dynamic_module_type_on_network_filter_data_status
envoy_dynamic_module_on_network_filter_new_connection(
    envoy_dynamic_module_type_network_filter_envoy_ptr filter_envoy_ptr,
    envoy_dynamic_module_type_network_filter_module_ptr filter_module_ptr);

/**
 * envoy_dynamic_module_on_network_filter_read is called when data is read from the connection
 * (downstream -> upstream direction).
 *
 * @param filter_envoy_ptr is the pointer to the DynamicModuleNetworkFilter object of the
 * corresponding network filter.
 * @param filter_module_ptr is the pointer to the in-module network filter created by
 * envoy_dynamic_module_on_network_filter_new.
 * @param data_length is the total length of the read data buffer.
 * @param end_stream is true if this is the last data (half-close from downstream).
 * @return envoy_dynamic_module_type_on_network_filter_data_status is the status of the filter.
 */
envoy_dynamic_module_type_on_network_filter_data_status envoy_dynamic_module_on_network_filter_read(
    envoy_dynamic_module_type_network_filter_envoy_ptr filter_envoy_ptr,
    envoy_dynamic_module_type_network_filter_module_ptr filter_module_ptr, size_t data_length,
    bool end_stream);

/**
 * envoy_dynamic_module_on_network_filter_write is called when data is to be written to the
 * connection (upstream -> downstream direction).
 *
 * @param filter_envoy_ptr is the pointer to the DynamicModuleNetworkFilter object of the
 * corresponding network filter.
 * @param filter_module_ptr is the pointer to the in-module network filter created by
 * envoy_dynamic_module_on_network_filter_new.
 * @param data_length is the total length of the write data buffer.
 * @param end_stream is true if this is the last data.
 * @return envoy_dynamic_module_type_on_network_filter_data_status is the status of the filter.
 */
envoy_dynamic_module_type_on_network_filter_data_status
envoy_dynamic_module_on_network_filter_write(
    envoy_dynamic_module_type_network_filter_envoy_ptr filter_envoy_ptr,
    envoy_dynamic_module_type_network_filter_module_ptr filter_module_ptr, size_t data_length,
    bool end_stream);

/**
 * envoy_dynamic_module_on_network_filter_event is called when a connection event occurs.
 *
 * @param filter_envoy_ptr is the pointer to the DynamicModuleNetworkFilter object of the
 * corresponding network filter.
 * @param filter_module_ptr is the pointer to the in-module network filter created by
 * envoy_dynamic_module_on_network_filter_new.
 * @param event is the connection event type.
 */
void envoy_dynamic_module_on_network_filter_event(
    envoy_dynamic_module_type_network_filter_envoy_ptr filter_envoy_ptr,
    envoy_dynamic_module_type_network_filter_module_ptr filter_module_ptr,
    envoy_dynamic_module_type_network_connection_event event);

/**
 * envoy_dynamic_module_on_network_filter_destroy is called when the network filter is destroyed
 * for each TCP connection.
 *
 * @param filter_module_ptr is the pointer to the in-module network filter.
 */
void envoy_dynamic_module_on_network_filter_destroy(
    envoy_dynamic_module_type_network_filter_module_ptr filter_module_ptr);

/**
 * envoy_dynamic_module_on_network_filter_http_callout_done is called when the HTTP callout
 * response is received initiated by a network filter.
 *
 * @param filter_envoy_ptr is the pointer to the DynamicModuleNetworkFilter object of the
 * corresponding network filter.
 * @param filter_module_ptr is the pointer to the in-module network filter created by
 * envoy_dynamic_module_on_network_filter_new.
 * @param callout_id is the ID of the callout. This is used to differentiate between multiple
 * calls.
 * @param result is the result of the callout.
 * @param headers is the headers of the response.
 * @param headers_size is the size of the headers.
 * @param body_chunks is the body of the response.
 * @param body_chunks_size is the size of the body.
 *
 * headers and body_chunks are owned by Envoy, and they are guaranteed to be valid until the end of
 * this event hook. They may be null if the callout fails or the response is empty.
 */
void envoy_dynamic_module_on_network_filter_http_callout_done(
    envoy_dynamic_module_type_network_filter_envoy_ptr filter_envoy_ptr,
    envoy_dynamic_module_type_network_filter_module_ptr filter_module_ptr, uint64_t callout_id,
    envoy_dynamic_module_type_http_callout_result result,
    envoy_dynamic_module_type_envoy_http_header* headers, size_t headers_size,
    envoy_dynamic_module_type_envoy_buffer* body_chunks, size_t body_chunks_size);

/**
 * envoy_dynamic_module_on_network_filter_scheduled is called when the event is scheduled via
 * envoy_dynamic_module_callback_network_filter_scheduler_commit callback.
 *
 * @param filter_envoy_ptr is the pointer to the DynamicModuleNetworkFilter object.
 * @param filter_module_ptr is the pointer to the in-module network filter.
 * @param event_id is the ID of the event. This is the same value as the one passed to
 * envoy_dynamic_module_callback_network_filter_scheduler_commit.
 */
void envoy_dynamic_module_on_network_filter_scheduled(
    envoy_dynamic_module_type_network_filter_envoy_ptr filter_envoy_ptr,
    envoy_dynamic_module_type_network_filter_module_ptr filter_module_ptr, uint64_t event_id);

/**
 * envoy_dynamic_module_on_network_filter_config_scheduled is called when the event is scheduled via
 * envoy_dynamic_module_callback_network_filter_config_scheduler_commit callback.
 *
 * @param filter_config_ptr is the pointer to the in-module network filter configuration.
 * @param event_id is the ID of the event. This is the same value as the one passed to
 * envoy_dynamic_module_callback_network_filter_config_scheduler_commit.
 */
void envoy_dynamic_module_on_network_filter_config_scheduled(
    envoy_dynamic_module_type_network_filter_config_module_ptr filter_config_ptr,
    uint64_t event_id);

/**
 * envoy_dynamic_module_on_network_filter_above_write_buffer_high_watermark is called when the
 * write buffer for the connection goes over its high watermark. This can be used to implement
 * flow control by disabling reads when the write buffer is full.
 *
 * @param filter_envoy_ptr is the pointer to the DynamicModuleNetworkFilter object of the
 * corresponding network filter.
 * @param filter_module_ptr is the pointer to the in-module network filter created by
 * envoy_dynamic_module_on_network_filter_new.
 */
void envoy_dynamic_module_on_network_filter_above_write_buffer_high_watermark(
    envoy_dynamic_module_type_network_filter_envoy_ptr filter_envoy_ptr,
    envoy_dynamic_module_type_network_filter_module_ptr filter_module_ptr);

/**
 * envoy_dynamic_module_on_network_filter_below_write_buffer_low_watermark is called when the
 * write buffer for the connection goes from over its high watermark to under its low watermark.
 * This can be used to re-enable reads after flow control was applied.
 *
 * @param filter_envoy_ptr is the pointer to the DynamicModuleNetworkFilter object of the
 * corresponding network filter.
 * @param filter_module_ptr is the pointer to the in-module network filter created by
 * envoy_dynamic_module_on_network_filter_new.
 */
void envoy_dynamic_module_on_network_filter_below_write_buffer_low_watermark(
    envoy_dynamic_module_type_network_filter_envoy_ptr filter_envoy_ptr,
    envoy_dynamic_module_type_network_filter_module_ptr filter_module_ptr);

// =============================================================================
// Network Filter Callbacks
// =============================================================================

// ---------------------- Socket Option Callbacks ----------------------------

/**
 * envoy_dynamic_module_callback_network_set_socket_option_int sets an integer socket option with
 * the given level, name, and state.
 *
 * @param filter_envoy_ptr is the pointer to the DynamicModuleNetworkFilter object.
 * @param level is the socket option level (e.g., SOL_SOCKET).
 * @param name is the socket option name (e.g., SO_KEEPALIVE).
 * @param state is the socket state at which this option should be applied.
 * @param value is the integer value for the socket option.
 */
void envoy_dynamic_module_callback_network_set_socket_option_int(
    envoy_dynamic_module_type_network_filter_envoy_ptr filter_envoy_ptr, int64_t level,
    int64_t name, envoy_dynamic_module_type_socket_option_state state, int64_t value);

/**
 * envoy_dynamic_module_callback_network_set_socket_option_bytes sets a bytes socket option with
 * the given level, name, and state.
 *
 * @param filter_envoy_ptr is the pointer to the DynamicModuleNetworkFilter object.
 * @param level is the socket option level.
 * @param name is the socket option name.
 * @param state is the socket state at which this option should be applied.
 * @param value is the byte buffer value for the socket option.
 */
void envoy_dynamic_module_callback_network_set_socket_option_bytes(
    envoy_dynamic_module_type_network_filter_envoy_ptr filter_envoy_ptr, int64_t level,
    int64_t name, envoy_dynamic_module_type_socket_option_state state,
    envoy_dynamic_module_type_module_buffer value);

/**
 * envoy_dynamic_module_callback_network_get_socket_option_int retrieves an integer socket option
 * value.
 *
 * @param filter_envoy_ptr is the pointer to the DynamicModuleNetworkFilter object.
 * @param level is the socket option level.
 * @param name is the socket option name.
 * @param state is the socket state.
 * @param value_out is the pointer to store the retrieved integer value.
 * @return true if the option is found, false otherwise.
 */
bool envoy_dynamic_module_callback_network_get_socket_option_int(
    envoy_dynamic_module_type_network_filter_envoy_ptr filter_envoy_ptr, int64_t level,
    int64_t name, envoy_dynamic_module_type_socket_option_state state, int64_t* value_out);

/**
 * envoy_dynamic_module_callback_network_get_socket_option_bytes retrieves a bytes socket option
 * value.
 *
 * @param filter_envoy_ptr is the pointer to the DynamicModuleNetworkFilter object.
 * @param level is the socket option level.
 * @param name is the socket option name.
 * @param state is the socket state.
 * @param value_out is the pointer to store the retrieved buffer. The buffer is owned by Envoy and
 * valid until the filter is destroyed.
 * @return true if the option is found, false otherwise.
 */
bool envoy_dynamic_module_callback_network_get_socket_option_bytes(
    envoy_dynamic_module_type_network_filter_envoy_ptr filter_envoy_ptr, int64_t level,
    int64_t name, envoy_dynamic_module_type_socket_option_state state,
    envoy_dynamic_module_type_envoy_buffer* value_out);

/**
 * envoy_dynamic_module_callback_network_get_socket_options_size returns the number of socket
 * options stored on the connection.
 *
 * @param filter_envoy_ptr is the pointer to the DynamicModuleNetworkFilter object.
 * @return the number of socket options.
 */
size_t envoy_dynamic_module_callback_network_get_socket_options_size(
    envoy_dynamic_module_type_network_filter_envoy_ptr filter_envoy_ptr);

/**
 * envoy_dynamic_module_callback_network_get_socket_options gets all socket options stored on the
 * connection. The caller should first call
 * envoy_dynamic_module_callback_network_get_socket_options_size to get the size, allocate an array
 * of that size, and pass the pointer to this function.
 *
 * @param filter_envoy_ptr is the pointer to the DynamicModuleNetworkFilter object.
 * @param options_out is the pointer to an array of socket options that will be filled. The array
 * must be pre-allocated by the caller with size equal to the value returned by
 * envoy_dynamic_module_callback_network_get_socket_options_size.
 */
void envoy_dynamic_module_callback_network_get_socket_options(
    envoy_dynamic_module_type_network_filter_envoy_ptr filter_envoy_ptr,
    envoy_dynamic_module_type_socket_option* options_out);

/**
 * envoy_dynamic_module_callback_network_filter_get_read_buffer_chunks_size is called by the module
 * to get the number of chunks in the current read data buffer. Combined with
 * envoy_dynamic_module_callback_network_filter_get_read_buffer_chunks, this can be used to iterate
 * over all chunks in the read buffer. This is valid after the first
 * envoy_dynamic_module_on_network_filter_read callback for the lifetime of the connection.
 *
 * @param filter_envoy_ptr is the pointer to the DynamicModuleNetworkFilter object.
 * @return the number of chunks in the read buffer. 0 if the buffer is not available or empty.
 */
size_t envoy_dynamic_module_callback_network_filter_get_read_buffer_chunks_size(
    envoy_dynamic_module_type_network_filter_envoy_ptr filter_envoy_ptr);

/**
 * envoy_dynamic_module_callback_network_filter_get_read_buffer_size is called by the module to
 * get the total size of the current read data buffer. This is valid after the first
 * envoy_dynamic_module_on_network_filter_read callback for the lifetime of the connection.
 *
 * @param filter_envoy_ptr is the pointer to the DynamicModuleNetworkFilter object.
 * @return the total size of the read buffer. 0 if the buffer is not available or empty.
 */
size_t envoy_dynamic_module_callback_network_filter_get_read_buffer_size(
    envoy_dynamic_module_type_network_filter_envoy_ptr filter_envoy_ptr);

/**
 * envoy_dynamic_module_callback_network_filter_get_read_buffer_chunks is called by the module to
 * get the current read data buffer as chunks. This is valid after the first
 * envoy_dynamic_module_on_network_filter_read callback for the lifetime of the connection.
 *
 * PRECONDITION: The module must ensure that the result_buffer_vector is valid and has enough length
 * to store all the chunks. The module can use
 * envoy_dynamic_module_callback_network_filter_get_read_buffer_chunks_size to get the number of
 * chunks before calling this function.
 *
 * @param filter_envoy_ptr is the pointer to the DynamicModuleNetworkFilter object.
 * @param result_buffer_vector is the pointer to the array of envoy_dynamic_module_type_envoy_buffer
 * where the chunks will be stored. The lifetime of the buffer is guaranteed until the end of the
 * current callback.
 * @return true if the buffer is available, false otherwise.
 */
bool envoy_dynamic_module_callback_network_filter_get_read_buffer_chunks(
    envoy_dynamic_module_type_network_filter_envoy_ptr filter_envoy_ptr,
    envoy_dynamic_module_type_envoy_buffer* result_buffer_vector);

/**
 * envoy_dynamic_module_callback_network_filter_get_write_buffer_chunks_size is called by the module
 * to get the number of chunks in the current write data buffer. Combined with
 * envoy_dynamic_module_callback_network_filter_get_write_buffer_chunks, this can be used to iterate
 * over all chunks in the write buffer. This is valid after the first
 * envoy_dynamic_module_on_network_filter_write callback for the lifetime of the connection.
 *
 * @param filter_envoy_ptr is the pointer to the DynamicModuleNetworkFilter object.
 * @return the number of chunks in the write buffer. 0 if the buffer is not available or empty.
 */
size_t envoy_dynamic_module_callback_network_filter_get_write_buffer_chunks_size(
    envoy_dynamic_module_type_network_filter_envoy_ptr filter_envoy_ptr);

/**
 * envoy_dynamic_module_callback_network_filter_get_write_buffer_size is called by the module to
 * get the total size of the current write data buffer. This is valid after the first
 * envoy_dynamic_module_on_network_filter_write callback for the lifetime of the connection.
 *
 * @param filter_envoy_ptr is the pointer to the DynamicModuleNetworkFilter object.
 * @return the total size of the write buffer. 0 if the buffer is not available or empty.
 */
size_t envoy_dynamic_module_callback_network_filter_get_write_buffer_size(
    envoy_dynamic_module_type_network_filter_envoy_ptr filter_envoy_ptr);

/**
 * envoy_dynamic_module_callback_network_filter_get_write_buffer_chunks is called by the module to
 * get the current write data buffer as chunks. This is valid after the first
 * envoy_dynamic_module_on_network_filter_write callback for the lifetime of the connection.
 *
 * PRECONDITION: The module must ensure that the result_buffer_vector is valid and has enough length
 * to store all the chunks. The module can use
 * envoy_dynamic_module_callback_network_filter_get_write_buffer_chunks_size to get the number of
 * chunks before calling this function.
 *
 * @param filter_envoy_ptr is the pointer to the DynamicModuleNetworkFilter object.
 * @param result_buffer_vector is the pointer to the array of envoy_dynamic_module_type_envoy_buffer
 * where the chunks will be stored. The lifetime of the buffer is guaranteed until the end of the
 * current callback.
 * @return true if the buffer is available, false otherwise.
 */
bool envoy_dynamic_module_callback_network_filter_get_write_buffer_chunks(
    envoy_dynamic_module_type_network_filter_envoy_ptr filter_envoy_ptr,
    envoy_dynamic_module_type_envoy_buffer* result_buffer_vector);

/**
 * envoy_dynamic_module_callback_network_filter_drain_read_buffer is called by the module to drain
 * bytes from the beginning of the read buffer.
 *
 * @param filter_envoy_ptr is the pointer to the DynamicModuleNetworkFilter object.
 * @param length is the number of bytes to drain from the beginning of the buffer.
 * @return true if the operation is successful, false otherwise.
 */
bool envoy_dynamic_module_callback_network_filter_drain_read_buffer(
    envoy_dynamic_module_type_network_filter_envoy_ptr filter_envoy_ptr, size_t length);

/**
 * envoy_dynamic_module_callback_network_filter_drain_write_buffer is called by the module to drain
 * bytes from the beginning of the write buffer.
 *
 * @param filter_envoy_ptr is the pointer to the DynamicModuleNetworkFilter object.
 * @param length is the number of bytes to drain from the beginning of the buffer.
 * @return true if the operation is successful, false otherwise.
 */
bool envoy_dynamic_module_callback_network_filter_drain_write_buffer(
    envoy_dynamic_module_type_network_filter_envoy_ptr filter_envoy_ptr, size_t length);

/**
 * envoy_dynamic_module_callback_network_filter_prepend_read_buffer is called by the module to
 * prepend data to the beginning of the read buffer.
 *
 * @param filter_envoy_ptr is the pointer to the DynamicModuleNetworkFilter object.
 * @param data is the data to prepend owned by the module.
 * @return true if the operation is successful, false otherwise.
 */
bool envoy_dynamic_module_callback_network_filter_prepend_read_buffer(
    envoy_dynamic_module_type_network_filter_envoy_ptr filter_envoy_ptr,
    envoy_dynamic_module_type_module_buffer data);

/**
 * envoy_dynamic_module_callback_network_filter_append_read_buffer is called by the module to
 * append data to the end of the read buffer.
 *
 * @param filter_envoy_ptr is the pointer to the DynamicModuleNetworkFilter object.
 * @param data is the data to append owned by the module.
 * @return true if the operation is successful, false otherwise.
 */
bool envoy_dynamic_module_callback_network_filter_append_read_buffer(
    envoy_dynamic_module_type_network_filter_envoy_ptr filter_envoy_ptr,
    envoy_dynamic_module_type_module_buffer data);

/**
 * envoy_dynamic_module_callback_network_filter_prepend_write_buffer is called by the module to
 * prepend data to the beginning of the write buffer.
 *
 * @param filter_envoy_ptr is the pointer to the DynamicModuleNetworkFilter object.
 * @param data is the data to prepend owned by the module.
 * @return true if the operation is successful, false otherwise.
 */
bool envoy_dynamic_module_callback_network_filter_prepend_write_buffer(
    envoy_dynamic_module_type_network_filter_envoy_ptr filter_envoy_ptr,
    envoy_dynamic_module_type_module_buffer data);

/**
 * envoy_dynamic_module_callback_network_filter_append_write_buffer is called by the module to
 * append data to the end of the write buffer.
 *
 * @param filter_envoy_ptr is the pointer to the DynamicModuleNetworkFilter object.
 * @param data is the data to append owned by the module.
 * @return true if the operation is successful, false otherwise.
 */
bool envoy_dynamic_module_callback_network_filter_append_write_buffer(
    envoy_dynamic_module_type_network_filter_envoy_ptr filter_envoy_ptr,
    envoy_dynamic_module_type_module_buffer data);

/**
 * envoy_dynamic_module_callback_network_filter_write is called by the module to write data
 * directly to the connection (downstream).
 *
 * @param filter_envoy_ptr is the pointer to the DynamicModuleNetworkFilter object.
 * @param data is the data to write owned by the module.
 * @param end_stream is true to half-close the connection after writing.
 */
void envoy_dynamic_module_callback_network_filter_write(
    envoy_dynamic_module_type_network_filter_envoy_ptr filter_envoy_ptr,
    envoy_dynamic_module_type_module_buffer data, bool end_stream);

/**
 * envoy_dynamic_module_callback_network_filter_inject_read_data is called by the module to inject
 * data into the read filter chain (after this filter).
 *
 * @param filter_envoy_ptr is the pointer to the DynamicModuleNetworkFilter object.
 * @param data is the data to inject owned by the module.
 * @param end_stream is true if this is the last data.
 */
void envoy_dynamic_module_callback_network_filter_inject_read_data(
    envoy_dynamic_module_type_network_filter_envoy_ptr filter_envoy_ptr,
    envoy_dynamic_module_type_module_buffer data, bool end_stream);

/**
 * envoy_dynamic_module_type_socket_option_value_type represents the type of value stored in a
 * socket option.
 * envoy_dynamic_module_callback_network_filter_inject_write_data is called by the module to inject
 * data into the write filter chain (after this filter).
 *
 * @param filter_envoy_ptr is the pointer to the DynamicModuleNetworkFilter object.
 * @param data is the data to inject owned by the module.
 * @param end_stream is true if this is the last data.
 */
void envoy_dynamic_module_callback_network_filter_inject_write_data(
    envoy_dynamic_module_type_network_filter_envoy_ptr filter_envoy_ptr,
    envoy_dynamic_module_type_module_buffer data, bool end_stream);

/**
 * envoy_dynamic_module_callback_network_filter_continue_reading is called by the module to
 * continue reading after returning StopIteration from onNewConnection or onData.
 *
 * @param filter_envoy_ptr is the pointer to the DynamicModuleNetworkFilter object.
 */
void envoy_dynamic_module_callback_network_filter_continue_reading(
    envoy_dynamic_module_type_network_filter_envoy_ptr filter_envoy_ptr);

/**
 * envoy_dynamic_module_callback_network_filter_close is called by the module to close the
 * connection.
 *
 * @param filter_envoy_ptr is the pointer to the DynamicModuleNetworkFilter object.
 * @param close_type specifies how to close the connection.
 */
void envoy_dynamic_module_callback_network_filter_close(
    envoy_dynamic_module_type_network_filter_envoy_ptr filter_envoy_ptr,
    envoy_dynamic_module_type_network_connection_close_type close_type);

/**
 * envoy_dynamic_module_callback_network_filter_get_connection_id is called by the module to get
 * the unique connection ID.
 *
 * @param filter_envoy_ptr is the pointer to the DynamicModuleNetworkFilter object.
 * @return the unique connection ID.
 */
uint64_t envoy_dynamic_module_callback_network_filter_get_connection_id(
    envoy_dynamic_module_type_network_filter_envoy_ptr filter_envoy_ptr);

/**
 * envoy_dynamic_module_callback_network_filter_get_remote_address is called by the module to get
 * the remote (client) address.
 *
 * @param filter_envoy_ptr is the pointer to the DynamicModuleNetworkFilter object.
 * @param address_out is the output pointer to the address string.
 * @param port_out is the output pointer to the port number.
 * @return true if the address is available, false otherwise.
 */
bool envoy_dynamic_module_callback_network_filter_get_remote_address(
    envoy_dynamic_module_type_network_filter_envoy_ptr filter_envoy_ptr,
    envoy_dynamic_module_type_envoy_buffer* address_out, uint32_t* port_out);

/**
 * envoy_dynamic_module_callback_network_filter_get_local_address is called by the module to get
 * the local address.
 *
 * @param filter_envoy_ptr is the pointer to the DynamicModuleNetworkFilter object.
 * @param address_out is the output pointer to the address string.
 * @param port_out is the output pointer to the port number.
 * @return true if the address is available, false otherwise.
 */
bool envoy_dynamic_module_callback_network_filter_get_local_address(
    envoy_dynamic_module_type_network_filter_envoy_ptr filter_envoy_ptr,
    envoy_dynamic_module_type_envoy_buffer* address_out, uint32_t* port_out);

/**
 * envoy_dynamic_module_callback_network_filter_is_ssl is called by the module to check if the
 * connection uses SSL/TLS.
 *
 * @param filter_envoy_ptr is the pointer to the DynamicModuleNetworkFilter object.
 * @return true if the connection uses SSL/TLS, false otherwise.
 */
bool envoy_dynamic_module_callback_network_filter_is_ssl(
    envoy_dynamic_module_type_network_filter_envoy_ptr filter_envoy_ptr);

/**
 * envoy_dynamic_module_callback_network_filter_disable_close is called by the module to disable
 * or enable connection close handling for this filter.
 *
 * @param filter_envoy_ptr is the pointer to the DynamicModuleNetworkFilter object.
 * @param disabled true to disable close handling, false to enable.
 */
void envoy_dynamic_module_callback_network_filter_disable_close(
    envoy_dynamic_module_type_network_filter_envoy_ptr filter_envoy_ptr, bool disabled);

/**
 * envoy_dynamic_module_callback_network_filter_close_with_details is called by the module to close
 * the connection with a specific close reason.
 *
 * @param filter_envoy_ptr is the pointer to the DynamicModuleNetworkFilter object.
 * @param close_type specifies how to close the connection.
 * @param details is the close reason string owned by the module. Can be empty.
 */
void envoy_dynamic_module_callback_network_filter_close_with_details(
    envoy_dynamic_module_type_network_filter_envoy_ptr filter_envoy_ptr,
    envoy_dynamic_module_type_network_connection_close_type close_type,
    envoy_dynamic_module_type_module_buffer details);

/**
 * envoy_dynamic_module_callback_network_filter_get_requested_server_name is called by the module
 * to get the requested server name (SNI) from the TLS handshake.
 *
 * @param filter_envoy_ptr is the pointer to the DynamicModuleNetworkFilter object.
 * @param result_out is the output buffer where the SNI string owned by Envoy will be stored.
 * @return true if SNI is available, false otherwise.
 */
bool envoy_dynamic_module_callback_network_filter_get_requested_server_name(
    envoy_dynamic_module_type_network_filter_envoy_ptr filter_envoy_ptr,
    envoy_dynamic_module_type_envoy_buffer* result_out);

/**
 * envoy_dynamic_module_callback_network_filter_get_direct_remote_address is called by the module
 * to get the direct remote (client) address without considering proxies or XFF.
 *
 * @param filter_envoy_ptr is the pointer to the DynamicModuleNetworkFilter object.
 * @param address_out is the output buffer where the address owned by Envoy will be stored.
 * @param port_out is the output pointer to the port number.
 * @return true if the address was found and is an IP address, false otherwise.
 */
bool envoy_dynamic_module_callback_network_filter_get_direct_remote_address(
    envoy_dynamic_module_type_network_filter_envoy_ptr filter_envoy_ptr,
    envoy_dynamic_module_type_envoy_buffer* address_out, uint32_t* port_out);

/**
 * envoy_dynamic_module_callback_network_filter_get_ssl_uri_sans_size is called by the module to
 * get the count of URI Subject Alternative Names from the peer certificate.
 *
 * @param filter_envoy_ptr is the pointer to the DynamicModuleNetworkFilter object.
 * @return the count of URI SANs, or 0 if SSL is not available.
 */
size_t envoy_dynamic_module_callback_network_filter_get_ssl_uri_sans_size(
    envoy_dynamic_module_type_network_filter_envoy_ptr filter_envoy_ptr);

/**
 * envoy_dynamic_module_callback_network_filter_get_ssl_uri_sans is called by the module to get
 * the URI Subject Alternative Names from the peer certificate. The module should first call
 * get_ssl_uri_sans_size to get the count and allocate the array.
 *
 * @param filter_envoy_ptr is the pointer to the DynamicModuleNetworkFilter object.
 * @param sans_out is a pre-allocated array owned by the module where Envoy will populate the SANs.
 *   The module must allocate this array with at least the size returned by get_ssl_uri_sans_size.
 * @return true if the SANs were populated successfully, false if SSL is not available.
 */
bool envoy_dynamic_module_callback_network_filter_get_ssl_uri_sans(
    envoy_dynamic_module_type_network_filter_envoy_ptr filter_envoy_ptr,
    envoy_dynamic_module_type_envoy_buffer* sans_out);

/**
 * envoy_dynamic_module_callback_network_filter_get_ssl_dns_sans_size is called by the module to
 * get the count of DNS Subject Alternative Names from the peer certificate.
 *
 * @param filter_envoy_ptr is the pointer to the DynamicModuleNetworkFilter object.
 * @return the count of DNS SANs, or 0 if SSL is not available.
 */
size_t envoy_dynamic_module_callback_network_filter_get_ssl_dns_sans_size(
    envoy_dynamic_module_type_network_filter_envoy_ptr filter_envoy_ptr);

/**
 * envoy_dynamic_module_callback_network_filter_get_ssl_dns_sans is called by the module to get
 * the DNS Subject Alternative Names from the peer certificate. The module should first call
 * get_ssl_dns_sans_size to get the count and allocate the array.
 *
 * @param filter_envoy_ptr is the pointer to the DynamicModuleNetworkFilter object.
 * @param sans_out is a pre-allocated array owned by the module where Envoy will populate the SANs.
 *   The module must allocate this array with at least the size returned by get_ssl_dns_sans_size.
 * @return true if the SANs were populated successfully, false if SSL is not available.
 */
bool envoy_dynamic_module_callback_network_filter_get_ssl_dns_sans(
    envoy_dynamic_module_type_network_filter_envoy_ptr filter_envoy_ptr,
    envoy_dynamic_module_type_envoy_buffer* sans_out);

/**
 * envoy_dynamic_module_callback_network_filter_get_ssl_subject is called by the module to get
 * the subject from the peer certificate.
 *
 * @param filter_envoy_ptr is the pointer to the DynamicModuleNetworkFilter object.
 * @param result_out is the output buffer where the subject owned by Envoy will be stored.
 * @return true if SSL is available, false otherwise.
 */
bool envoy_dynamic_module_callback_network_filter_get_ssl_subject(
    envoy_dynamic_module_type_network_filter_envoy_ptr filter_envoy_ptr,
    envoy_dynamic_module_type_envoy_buffer* result_out);

// ---------------------------- Filter State Callbacks -------------------------

/**
 * envoy_dynamic_module_callback_network_set_filter_state_bytes is called by the module to set
 * filter state with a bytes value. The filter state can be read by other filters in the chain and
 * can influence routing decisions (e.g., tcp_proxy cluster selection).
 *
 * @param filter_envoy_ptr is the pointer to the DynamicModuleNetworkFilter object.
 * @param key is the key name owned by the module.
 * @param value is the value owned by the module.
 * @return true if the operation is successful, false otherwise.
 */
bool envoy_dynamic_module_callback_network_set_filter_state_bytes(
    envoy_dynamic_module_type_network_filter_envoy_ptr filter_envoy_ptr,
    envoy_dynamic_module_type_module_buffer key, envoy_dynamic_module_type_module_buffer value);

/**
 * envoy_dynamic_module_callback_network_get_filter_state_bytes is called by the module to get
 * filter state bytes value.
 *
 * @param filter_envoy_ptr is the pointer to the DynamicModuleNetworkFilter object.
 * @param key is the key name owned by the module.
 * @param value_out is the output buffer where the value owned by Envoy will be stored.
 * @return true if the key exists and is a bytes value, false otherwise.
 */
bool envoy_dynamic_module_callback_network_get_filter_state_bytes(
    envoy_dynamic_module_type_network_filter_envoy_ptr filter_envoy_ptr,
    envoy_dynamic_module_type_module_buffer key, envoy_dynamic_module_type_envoy_buffer* value_out);

/**
 * envoy_dynamic_module_callback_network_set_filter_state_typed is called by the module to set
 * typed filter state using the registered ObjectFactory for the key. Unlike set_filter_state_bytes
 * which stores a raw StringAccessor, this creates a properly typed filter state object via
 * ObjectFactory::createFromBytes. This is required for interoperability with built-in Envoy
 * filters that read filter state as typed objects (e.g., tcp_proxy reads PerConnectionCluster
 * via the key "envoy.tcp_proxy.cluster").
 *
 * @param filter_envoy_ptr is the pointer to the DynamicModuleNetworkFilter object.
 * @param key is the key name owned by the module. This must match a registered ObjectFactory name.
 * @param value is the serialized bytes value used to construct the typed object.
 * @return true if the operation is successful, false if no ObjectFactory is registered for the
 * key, the factory fails to create the object, or the key already exists and is marked as
 * read-only.
 */
bool envoy_dynamic_module_callback_network_set_filter_state_typed(
    envoy_dynamic_module_type_network_filter_envoy_ptr filter_envoy_ptr,
    envoy_dynamic_module_type_module_buffer key, envoy_dynamic_module_type_module_buffer value);

/**
 * envoy_dynamic_module_callback_network_get_filter_state_typed is called by the module to get
 * the serialized bytes value of a typed filter state object with the given key. This retrieves
 * the object generically and calls serializeAsString to get the bytes representation. This works
 * with any filter state object type, not just StringAccessor.
 *
 * @param filter_envoy_ptr is the pointer to the DynamicModuleNetworkFilter object.
 * @param key is the key name owned by the module.
 * @param value_out is the output buffer where the serialized value owned by Envoy will be stored.
 * @return true if the key exists and the object supports serialization, false otherwise.
 *
 * Note that the buffer pointed by the pointer stored in value_out is owned by Envoy, and
 * they are guaranteed to be valid until the end of the current event hook unless the setter
 * callback is called.
 */
bool envoy_dynamic_module_callback_network_get_filter_state_typed(
    envoy_dynamic_module_type_network_filter_envoy_ptr filter_envoy_ptr,
    envoy_dynamic_module_type_module_buffer key, envoy_dynamic_module_type_envoy_buffer* value_out);

// ---------------------------- Dynamic Metadata Callbacks ---------------------

/**
 * envoy_dynamic_module_callback_network_set_dynamic_metadata_string is called by the module to
 * set the string value of the dynamic metadata with the given namespace and key. If the metadata
 * is existing, it will be overwritten.
 *
 * @param filter_envoy_ptr is the pointer to the DynamicModuleNetworkFilter object.
 * @param filter_namespace is the namespace owned by the module.
 * @param key is the key owned by the module.
 * @param value is the string value owned by the module.
 */
void envoy_dynamic_module_callback_network_set_dynamic_metadata_string(
    envoy_dynamic_module_type_network_filter_envoy_ptr filter_envoy_ptr,
    envoy_dynamic_module_type_module_buffer filter_namespace,
    envoy_dynamic_module_type_module_buffer key, envoy_dynamic_module_type_module_buffer value);

/**
 * envoy_dynamic_module_callback_network_get_dynamic_metadata_string is called by the module to
 * get the string value of the dynamic metadata with the given namespace and key. If the namespace
 * does not exist, the key does not exist, or the value is not a string, this returns false.
 *
 * @param filter_envoy_ptr is the pointer to the DynamicModuleNetworkFilter object.
 * @param filter_namespace is the namespace owned by the module.
 * @param key is the key owned by the module.
 * @param value_out is the output buffer where the value owned by Envoy will be stored.
 * @return true if the operation is successful, false otherwise.
 */
bool envoy_dynamic_module_callback_network_get_dynamic_metadata_string(
    envoy_dynamic_module_type_network_filter_envoy_ptr filter_envoy_ptr,
    envoy_dynamic_module_type_module_buffer filter_namespace,
    envoy_dynamic_module_type_module_buffer key, envoy_dynamic_module_type_envoy_buffer* value_out);

/**
 * envoy_dynamic_module_callback_network_set_dynamic_metadata_number is called by the module to
 * set the number value of the dynamic metadata with the given namespace and key. If the metadata
 * is existing, it will be overwritten.
 *
 * @param filter_envoy_ptr is the pointer to the DynamicModuleNetworkFilter object.
 * @param filter_namespace is the namespace owned by the module.
 * @param key is the key owned by the module.
 * @param value is the number value of the dynamic metadata to be set.
 */
void envoy_dynamic_module_callback_network_set_dynamic_metadata_number(
    envoy_dynamic_module_type_network_filter_envoy_ptr filter_envoy_ptr,
    envoy_dynamic_module_type_module_buffer filter_namespace,
    envoy_dynamic_module_type_module_buffer key, double value);

/**
 * envoy_dynamic_module_callback_network_get_dynamic_metadata_number is called by the module to
 * get the number value of the dynamic metadata with the given namespace and key. If the namespace
 * does not exist, the key does not exist, or the value is not a number, this returns false.
 *
 * @param filter_envoy_ptr is the pointer to the DynamicModuleNetworkFilter object.
 * @param filter_namespace is the namespace owned by the module.
 * @param key is the key owned by the module.
 * @param result is the output pointer to the number value of the dynamic metadata.
 * @return true if the operation is successful, false otherwise.
 */
bool envoy_dynamic_module_callback_network_get_dynamic_metadata_number(
    envoy_dynamic_module_type_network_filter_envoy_ptr filter_envoy_ptr,
    envoy_dynamic_module_type_module_buffer filter_namespace,
    envoy_dynamic_module_type_module_buffer key, double* result);

/**
 * envoy_dynamic_module_callback_network_set_dynamic_metadata_bool is called by the module to
 * set the bool value of the dynamic metadata with the given namespace and key. If the metadata
 * is existing, it will be overwritten.
 *
 * @param filter_envoy_ptr is the pointer to the DynamicModuleNetworkFilter object.
 * @param filter_namespace is the namespace owned by the module.
 * @param key is the key owned by the module.
 * @param value is the bool value of the dynamic metadata to be set.
 */
void envoy_dynamic_module_callback_network_set_dynamic_metadata_bool(
    envoy_dynamic_module_type_network_filter_envoy_ptr filter_envoy_ptr,
    envoy_dynamic_module_type_module_buffer filter_namespace,
    envoy_dynamic_module_type_module_buffer key, bool value);

/**
 * envoy_dynamic_module_callback_network_get_dynamic_metadata_bool is called by the module to
 * get the bool value of the dynamic metadata with the given namespace and key. If the namespace
 * does not exist, the key does not exist, or the value is not a bool, this returns false.
 *
 * @param filter_envoy_ptr is the pointer to the DynamicModuleNetworkFilter object.
 * @param filter_namespace is the namespace owned by the module.
 * @param key is the key owned by the module.
 * @param result is the output pointer to the bool value of the dynamic metadata.
 * @return true if the operation is successful, false otherwise.
 */
bool envoy_dynamic_module_callback_network_get_dynamic_metadata_bool(
    envoy_dynamic_module_type_network_filter_envoy_ptr filter_envoy_ptr,
    envoy_dynamic_module_type_module_buffer filter_namespace,
    envoy_dynamic_module_type_module_buffer key, bool* result);

// ------------------------------ HTTP Callouts -------------------------------

/**
 * envoy_dynamic_module_callback_network_filter_http_callout is called by the module to initiate an
 * HTTP callout. The callout is initiated by the network filter and the response is received in
 * envoy_dynamic_module_on_network_filter_http_callout_done.
 *
 * @param filter_envoy_ptr is the pointer to the DynamicModuleNetworkFilter object of the
 * corresponding network filter.
 * @param callout_id_out is a pointer to a variable where the callout ID will be stored. This can be
 * arbitrary and is used to differentiate between multiple calls from the same filter.
 * @param cluster_name is the name of the cluster to which the callout is sent.
 * @param headers is the headers of the request. It must contain :method, :path and host headers.
 * @param headers_size is the size of the headers.
 * @param body is the body of the request.
 * @param timeout_milliseconds is the timeout for the callout in milliseconds.
 * @return envoy_dynamic_module_type_http_callout_init_result is the result of the callout
 * initialization.
 */
envoy_dynamic_module_type_http_callout_init_result
envoy_dynamic_module_callback_network_filter_http_callout(
    envoy_dynamic_module_type_network_filter_envoy_ptr filter_envoy_ptr, uint64_t* callout_id_out,
    envoy_dynamic_module_type_module_buffer cluster_name,
    envoy_dynamic_module_type_module_http_header* headers, size_t headers_size,
    envoy_dynamic_module_type_module_buffer body, uint64_t timeout_milliseconds);

// -------------------- Network Filter Callbacks - Metrics -----------------

/**
 * envoy_dynamic_module_callback_network_filter_config_define_counter is called by the module
 * during initialization to create a new Stats::Counter with the given name.
 *
 * @param config_envoy_ptr is the pointer to the DynamicModuleNetworkFilterConfig in which the
 * counter will be defined.
 * @param name is the name of the counter to be defined.
 * @param counter_id_ptr where the opaque ID that represents a unique metric will be stored. This
 * can be passed to envoy_dynamic_module_callback_network_filter_increment_counter together with
 * filter_envoy_ptr.
 * @return the result of the operation.
 */
envoy_dynamic_module_type_metrics_result
envoy_dynamic_module_callback_network_filter_config_define_counter(
    envoy_dynamic_module_type_network_filter_config_envoy_ptr config_envoy_ptr,
    envoy_dynamic_module_type_module_buffer name, size_t* counter_id_ptr);

/**
 * envoy_dynamic_module_callback_network_filter_increment_counter is called by the module to
 * increment a previously defined counter.
 *
 * @param filter_envoy_ptr is the pointer to the DynamicModuleNetworkFilter object.
 * @param id is the ID of the counter previously defined using the config.
 * @param value is the value to increment the counter by.
 * @return the result of the operation.
 */
envoy_dynamic_module_type_metrics_result
envoy_dynamic_module_callback_network_filter_increment_counter(
    envoy_dynamic_module_type_network_filter_envoy_ptr filter_envoy_ptr, size_t id, uint64_t value);

/**
 * envoy_dynamic_module_callback_network_filter_config_define_gauge is called by the module during
 * initialization to create a new Stats::Gauge with the given name.
 *
 * @param config_envoy_ptr is the pointer to the DynamicModuleNetworkFilterConfig in which the
 * gauge will be defined.
 * @param name is the name of the gauge to be defined.
 * @param gauge_id_ptr where the opaque ID that represents a unique metric will be stored.
 * @return the result of the operation.
 */
envoy_dynamic_module_type_metrics_result
envoy_dynamic_module_callback_network_filter_config_define_gauge(
    envoy_dynamic_module_type_network_filter_config_envoy_ptr config_envoy_ptr,
    envoy_dynamic_module_type_module_buffer name, size_t* gauge_id_ptr);

/**
 * envoy_dynamic_module_callback_network_filter_set_gauge is called by the module to set the value
 * of a previously defined gauge.
 *
 * @param filter_envoy_ptr is the pointer to the DynamicModuleNetworkFilter object.
 * @param id is the ID of the gauge previously defined using the config.
 * @param value is the value to set the gauge to.
 * @return the result of the operation.
 */
envoy_dynamic_module_type_metrics_result envoy_dynamic_module_callback_network_filter_set_gauge(
    envoy_dynamic_module_type_network_filter_envoy_ptr filter_envoy_ptr, size_t id, uint64_t value);

/**
 * envoy_dynamic_module_callback_network_filter_increment_gauge is called by the module to increase
 * the value of a previously defined gauge.
 *
 * @param filter_envoy_ptr is the pointer to the DynamicModuleNetworkFilter object.
 * @param id is the ID of the gauge previously defined using the config.
 * @param value is the value to increase the gauge by.
 * @return the result of the operation.
 */
envoy_dynamic_module_type_metrics_result
envoy_dynamic_module_callback_network_filter_increment_gauge(
    envoy_dynamic_module_type_network_filter_envoy_ptr filter_envoy_ptr, size_t id, uint64_t value);

/**
 * envoy_dynamic_module_callback_network_filter_decrement_gauge is called by the module to decrease
 * the value of a previously defined gauge.
 *
 * @param filter_envoy_ptr is the pointer to the DynamicModuleNetworkFilter object.
 * @param id is the ID of the gauge previously defined using the config.
 * @param value is the value to decrease the gauge by.
 * @return the result of the operation.
 */
envoy_dynamic_module_type_metrics_result
envoy_dynamic_module_callback_network_filter_decrement_gauge(
    envoy_dynamic_module_type_network_filter_envoy_ptr filter_envoy_ptr, size_t id, uint64_t value);

/**
 * envoy_dynamic_module_callback_network_filter_config_define_histogram is called by the module
 * during initialization to create a new Stats::Histogram with the given name.
 *
 * @param config_envoy_ptr is the pointer to the DynamicModuleNetworkFilterConfig in which the
 * histogram will be defined.
 * @param name is the name of the histogram to be defined.
 * @param histogram_id_ptr where the opaque ID that represents a unique metric will be stored.
 * @return the result of the operation.
 */
envoy_dynamic_module_type_metrics_result
envoy_dynamic_module_callback_network_filter_config_define_histogram(
    envoy_dynamic_module_type_network_filter_config_envoy_ptr config_envoy_ptr,
    envoy_dynamic_module_type_module_buffer name, size_t* histogram_id_ptr);

/**
 * envoy_dynamic_module_callback_network_filter_record_histogram_value is called by the module to
 * record a value in a previously defined histogram.
 *
 * @param filter_envoy_ptr is the pointer to the DynamicModuleNetworkFilter object.
 * @param id is the ID of the histogram previously defined using the config.
 * @param value is the value to record in the histogram.
 * @return the result of the operation.
 */
envoy_dynamic_module_type_metrics_result
envoy_dynamic_module_callback_network_filter_record_histogram_value(
    envoy_dynamic_module_type_network_filter_envoy_ptr filter_envoy_ptr, size_t id, uint64_t value);

// ---------------------- Upstream Host Access Callbacks -----------------------

/**
 * envoy_dynamic_module_callback_network_filter_get_cluster_host_count retrieves the host counts for
 * a cluster by name. This provides visibility into the cluster's health state and can be used to
 * implement scale-to-zero logic or custom load balancing decisions.
 *
 * @param filter_envoy_ptr is the pointer to the DynamicModuleNetworkFilter object.
 * @param cluster_name is the name of the cluster to query owned by the module.
 * @param priority is the priority level to query (0 for default priority).
 * @param total_count is the pointer to store the total number of hosts. Can be null if not needed.
 * @param healthy_count is the pointer to store the number of healthy hosts. Can be null if not
 * needed.
 * @param degraded_count is the pointer to store the number of degraded hosts. Can be null if not
 * needed.
 * @return true if the counts were retrieved successfully, false otherwise (e.g., cluster not
 * found).
 */
bool envoy_dynamic_module_callback_network_filter_get_cluster_host_count(
    envoy_dynamic_module_type_network_filter_envoy_ptr filter_envoy_ptr,
    envoy_dynamic_module_type_module_buffer cluster_name, uint32_t priority, size_t* total_count,
    size_t* healthy_count, size_t* degraded_count);

/**
 * envoy_dynamic_module_callback_network_filter_get_upstream_host_address is called by the module
 * to get the address and port of the currently selected upstream host.
 *
 * @param filter_envoy_ptr is the pointer to the DynamicModuleNetworkFilter object.
 * @param address_out is the output buffer where the address string owned by Envoy will be stored.
 * @param port_out is the output pointer to the port number.
 * @return true if the upstream host is set and has an IP address, false otherwise.
 */
bool envoy_dynamic_module_callback_network_filter_get_upstream_host_address(
    envoy_dynamic_module_type_network_filter_envoy_ptr filter_envoy_ptr,
    envoy_dynamic_module_type_envoy_buffer* address_out, uint32_t* port_out);

/**
 * envoy_dynamic_module_callback_network_filter_get_upstream_host_hostname is called by the module
 * to get the hostname of the currently selected upstream host.
 *
 * @param filter_envoy_ptr is the pointer to the DynamicModuleNetworkFilter object.
 * @param hostname_out is the output buffer where the hostname string owned by Envoy will be stored.
 * @return true if the upstream host is set and has a hostname, false otherwise.
 */
bool envoy_dynamic_module_callback_network_filter_get_upstream_host_hostname(
    envoy_dynamic_module_type_network_filter_envoy_ptr filter_envoy_ptr,
    envoy_dynamic_module_type_envoy_buffer* hostname_out);

/**
 * envoy_dynamic_module_callback_network_filter_get_upstream_host_cluster is called by the module
 * to get the cluster name of the currently selected upstream host.
 *
 * @param filter_envoy_ptr is the pointer to the DynamicModuleNetworkFilter object.
 * @param cluster_name_out is the output buffer where the cluster name string owned by Envoy will
 * be stored.
 * @return true if the upstream host is set, false otherwise.
 */
bool envoy_dynamic_module_callback_network_filter_get_upstream_host_cluster(
    envoy_dynamic_module_type_network_filter_envoy_ptr filter_envoy_ptr,
    envoy_dynamic_module_type_envoy_buffer* cluster_name_out);

/**
 * envoy_dynamic_module_callback_network_filter_has_upstream_host is called by the module to check
 * if an upstream host has been selected for this connection.
 *
 * @param filter_envoy_ptr is the pointer to the DynamicModuleNetworkFilter object.
 * @return true if an upstream host is set, false otherwise.
 */
bool envoy_dynamic_module_callback_network_filter_has_upstream_host(
    envoy_dynamic_module_type_network_filter_envoy_ptr filter_envoy_ptr);

// ---------------------- StartTLS Support Callbacks ---------------------------

/**
 * envoy_dynamic_module_callback_network_filter_start_upstream_secure_transport is called by the
 * module to convert the upstream connection from non-secure to secure mode (StartTLS).
 *
 * This signals the filter manager to enable secure transport mode in the upstream connection.
 * This is done when the upstream connection's transport socket is of startTLS type. At the moment
 * it is the only transport socket type which can be programmatically converted from non-secure
 * mode to secure mode.
 *
 * @param filter_envoy_ptr is the pointer to the DynamicModuleNetworkFilter object.
 * @return true if the upstream transport was successfully converted to secure mode, false
 * otherwise.
 */
bool envoy_dynamic_module_callback_network_filter_start_upstream_secure_transport(
    envoy_dynamic_module_type_network_filter_envoy_ptr filter_envoy_ptr);

// ---------------------- Connection State and Flow Control Callbacks ----------

/**
 * envoy_dynamic_module_callback_network_filter_get_connection_state is called by the module to get
 * the current state of the connection.
 *
 * @param filter_envoy_ptr is the pointer to the DynamicModuleNetworkFilter object.
 * @return the current connection state (Open, Closing, or Closed).
 */
envoy_dynamic_module_type_network_connection_state
envoy_dynamic_module_callback_network_filter_get_connection_state(
    envoy_dynamic_module_type_network_filter_envoy_ptr filter_envoy_ptr);

/**
 * envoy_dynamic_module_callback_network_filter_read_disable is called by the module to disable
 * or enable reading from the connection. This is the primary mechanism for implementing
 * back-pressure in TCP filters.
 *
 * When reads are disabled, no more data will be read from the socket. When re-enabled, if there
 * is data in the input buffer, it will be re-dispatched through the filter chain.
 *
 * Note that this function reference counts calls. For example:
 *   read_disable(true);  // Disables reading
 *   read_disable(true);  // Notes the connection is blocked by two sources
 *   read_disable(false); // Notes the connection is blocked by one source
 *   read_disable(false); // Marks the connection as unblocked, so resumes reading
 *
 * @param filter_envoy_ptr is the pointer to the DynamicModuleNetworkFilter object.
 * @param disable is true to disable reading, false to enable reading.
 * @return the status indicating the outcome of the operation.
 */
envoy_dynamic_module_type_network_read_disable_status
envoy_dynamic_module_callback_network_filter_read_disable(
    envoy_dynamic_module_type_network_filter_envoy_ptr filter_envoy_ptr, bool disable);

/**
 * envoy_dynamic_module_callback_network_filter_read_enabled is called by the module to check if
 * reading is currently enabled on the connection.
 *
 * @param filter_envoy_ptr is the pointer to the DynamicModuleNetworkFilter object.
 * @return true if reading is enabled, false if reading is disabled.
 */
bool envoy_dynamic_module_callback_network_filter_read_enabled(
    envoy_dynamic_module_type_network_filter_envoy_ptr filter_envoy_ptr);

/**
 * envoy_dynamic_module_callback_network_filter_is_half_close_enabled is called by the module to
 * check if half-close semantics are enabled on this connection.
 *
 * When half-close is enabled, reading a remote half-close will not fully close the connection.
 *
 * @param filter_envoy_ptr is the pointer to the DynamicModuleNetworkFilter object.
 * @return true if half-close semantics are enabled, false otherwise.
 */
bool envoy_dynamic_module_callback_network_filter_is_half_close_enabled(
    envoy_dynamic_module_type_network_filter_envoy_ptr filter_envoy_ptr);

/**
 * envoy_dynamic_module_callback_network_filter_enable_half_close is called by the module to enable
 * or disable half-close semantics on the connection.
 *
 * When half-close is enabled, reading a remote half-close will not fully close the connection,
 * allowing the filter to continue writing data.
 *
 * @param filter_envoy_ptr is the pointer to the DynamicModuleNetworkFilter object.
 * @param enabled is true to enable half-close semantics, false to disable.
 */
void envoy_dynamic_module_callback_network_filter_enable_half_close(
    envoy_dynamic_module_type_network_filter_envoy_ptr filter_envoy_ptr, bool enabled);

/**
 * envoy_dynamic_module_callback_network_filter_get_buffer_limit is called by the module to get
 * the current buffer limit set on the connection.
 *
 * @param filter_envoy_ptr is the pointer to the DynamicModuleNetworkFilter object.
 * @return the current buffer limit in bytes.
 */
uint32_t envoy_dynamic_module_callback_network_filter_get_buffer_limit(
    envoy_dynamic_module_type_network_filter_envoy_ptr filter_envoy_ptr);

/**
 * envoy_dynamic_module_callback_network_filter_set_buffer_limits is called by the module to set
 * a soft limit on the size of buffers for the connection.
 *
 * For the read buffer, this limits the bytes read prior to flushing to further stages in the
 * processing pipeline. For the write buffer, it sets watermarks. When enough data is buffered,
 * it triggers envoy_dynamic_module_on_network_filter_above_write_buffer_high_watermark callback.
 * When enough data is drained from the write buffer,
 * envoy_dynamic_module_on_network_filter_below_write_buffer_low_watermark is called.
 *
 * @param filter_envoy_ptr is the pointer to the DynamicModuleNetworkFilter object.
 * @param limit is the buffer limit in bytes.
 */
void envoy_dynamic_module_callback_network_filter_set_buffer_limits(
    envoy_dynamic_module_type_network_filter_envoy_ptr filter_envoy_ptr, uint32_t limit);

/**
 * envoy_dynamic_module_callback_network_filter_above_high_watermark is called by the module to
 * check if the connection is currently above the high watermark.
 *
 * @param filter_envoy_ptr is the pointer to the DynamicModuleNetworkFilter object.
 * @return true if the connection is above the high watermark, false otherwise.
 */
bool envoy_dynamic_module_callback_network_filter_above_high_watermark(
    envoy_dynamic_module_type_network_filter_envoy_ptr filter_envoy_ptr);

// ---------------------- Network filter scheduler callbacks -------------------

/**
 * envoy_dynamic_module_callback_network_filter_scheduler_new is called by the module to create a
 * new network filter scheduler. The scheduler is used to dispatch network filter operations from
 * any thread including the ones managed by the module.
 *
 * @param filter_envoy_ptr is the pointer to the DynamicModuleNetworkFilter object of the
 * corresponding network filter.
 * @return envoy_dynamic_module_type_network_filter_scheduler_module_ptr is the pointer to the
 * created network filter scheduler.
 *
 * NOTE: it is caller's responsibility to delete the scheduler using
 * envoy_dynamic_module_callback_network_filter_scheduler_delete when it is no longer needed.
 * See the comment on envoy_dynamic_module_type_network_filter_scheduler_module_ptr.
 */
envoy_dynamic_module_type_network_filter_scheduler_module_ptr
envoy_dynamic_module_callback_network_filter_scheduler_new(
    envoy_dynamic_module_type_network_filter_envoy_ptr filter_envoy_ptr);

/**
 * envoy_dynamic_module_callback_network_filter_scheduler_commit is called by the module to
 * schedule a generic event to the network filter on the worker thread it is running on.
 *
 * This will eventually end up invoking envoy_dynamic_module_on_network_filter_scheduled
 * event hook on the worker thread.
 *
 * This can be called multiple times to schedule multiple events to the same filter.
 *
 * @param scheduler_module_ptr is the pointer to the network filter scheduler created by
 * envoy_dynamic_module_callback_network_filter_scheduler_new.
 * @param event_id is the ID of the event. This can be used to differentiate between multiple
 * events scheduled to the same filter. It can be any module-defined value.
 */
void envoy_dynamic_module_callback_network_filter_scheduler_commit(
    envoy_dynamic_module_type_network_filter_scheduler_module_ptr scheduler_module_ptr,
    uint64_t event_id);

/**
 * envoy_dynamic_module_callback_network_filter_scheduler_delete is called by the module to delete
 * the network filter scheduler created by
 * envoy_dynamic_module_callback_network_filter_scheduler_new.
 *
 * @param scheduler_module_ptr is the pointer to the network filter scheduler created by
 * envoy_dynamic_module_callback_network_filter_scheduler_new.
 */
void envoy_dynamic_module_callback_network_filter_scheduler_delete(
    envoy_dynamic_module_type_network_filter_scheduler_module_ptr scheduler_module_ptr);

/**
 * envoy_dynamic_module_callback_network_filter_config_scheduler_new is called by the module to
 * create a new network filter configuration scheduler. The scheduler is used to dispatch network
 * filter configuration operations to the main thread from any thread including the ones managed by
 * the module.
 *
 * @param filter_config_envoy_ptr is the pointer to the DynamicModuleNetworkFilterConfig object.
 * @return envoy_dynamic_module_type_network_filter_config_scheduler_module_ptr is the pointer to
 * the created network filter configuration scheduler.
 *
 * NOTE: it is caller's responsibility to delete the scheduler using
 * envoy_dynamic_module_callback_network_filter_config_scheduler_delete when it is no longer needed.
 * See the comment on envoy_dynamic_module_type_network_filter_config_scheduler_module_ptr.
 */
envoy_dynamic_module_type_network_filter_config_scheduler_module_ptr
envoy_dynamic_module_callback_network_filter_config_scheduler_new(
    envoy_dynamic_module_type_network_filter_config_envoy_ptr filter_config_envoy_ptr);

/**
 * envoy_dynamic_module_callback_network_filter_config_scheduler_delete is called by the module to
 * delete the network filter configuration scheduler created by
 * envoy_dynamic_module_callback_network_filter_config_scheduler_new.
 *
 * @param scheduler_module_ptr is the pointer to the network filter configuration scheduler created
 * by envoy_dynamic_module_callback_network_filter_config_scheduler_new.
 */
void envoy_dynamic_module_callback_network_filter_config_scheduler_delete(
    envoy_dynamic_module_type_network_filter_config_scheduler_module_ptr scheduler_module_ptr);

/**
 * envoy_dynamic_module_callback_network_filter_config_scheduler_commit is called by the module to
 * schedule a generic event to the network filter configuration on the main thread.
 *
 * This will eventually end up invoking envoy_dynamic_module_on_network_filter_config_scheduled
 * event hook on the main thread.
 *
 * This can be called multiple times to schedule multiple events to the same filter configuration.
 *
 * @param scheduler_module_ptr is the pointer to the network filter configuration scheduler created
 * by envoy_dynamic_module_callback_network_filter_config_scheduler_new.
 * @param event_id is the ID of the event. This can be used to differentiate between multiple
 * events scheduled to the same filter configuration. It can be any module-defined value.
 */
void envoy_dynamic_module_callback_network_filter_config_scheduler_commit(
    envoy_dynamic_module_type_network_filter_config_scheduler_module_ptr scheduler_module_ptr,
    uint64_t event_id);

// --------------------- Network Filter Callbacks - Misc ---------------

/**
 * envoy_dynamic_module_callback_network_filter_get_worker_index is called by the module to get the
 * worker index assigned to the current network filter. This can be used by the module to manage
 * worker-specific resources or perform worker-specific logic.
 * @param filter_envoy_ptr is the pointer to the DynamicModuleNetworkFilter object of the
 * corresponding network filter.
 * @return the worker index assigned to the current network filter.
 */
uint32_t envoy_dynamic_module_callback_network_filter_get_worker_index(
    envoy_dynamic_module_type_network_filter_envoy_ptr filter_envoy_ptr);

// =============================================================================
// ============================= Listener Filter ===============================
// =============================================================================

// =============================================================================
// Listener Filter Types
// =============================================================================

/**
 * envoy_dynamic_module_type_listener_filter_config_envoy_ptr is a raw pointer to
 * the DynamicModuleListenerFilterConfig class in Envoy. This is passed to the module when
 * creating a new in-module listener filter configuration and used to access the listener
 * filter-scoped information.
 *
 * This has 1:1 correspondence with envoy_dynamic_module_type_listener_filter_config_module_ptr in
 * the module.
 *
 * OWNERSHIP: Envoy owns the pointer.
 */
typedef void* envoy_dynamic_module_type_listener_filter_config_envoy_ptr;

/**
 * envoy_dynamic_module_type_listener_filter_config_module_ptr is a pointer to an in-module listener
 * filter configuration corresponding to an Envoy listener filter configuration. The config is
 * responsible for creating a new listener filter that corresponds to each accepted connection.
 *
 * This has 1:1 correspondence with the DynamicModuleListenerFilterConfig class in Envoy.
 *
 * OWNERSHIP: The module is responsible for managing the lifetime of the pointer. The pointer can be
 * released when envoy_dynamic_module_on_listener_filter_config_destroy is called for the same
 * pointer.
 */
typedef const void* envoy_dynamic_module_type_listener_filter_config_module_ptr;

/**
 * envoy_dynamic_module_type_listener_filter_envoy_ptr is a raw pointer to the
 * DynamicModuleListenerFilter class in Envoy. This is passed to the module when creating a new
 * listener filter for each accepted connection and used to access the listener filter-scoped
 * information such as socket data, buffers, etc.
 *
 * This has 1:1 correspondence with envoy_dynamic_module_type_listener_filter_module_ptr in the
 * module.
 *
 * OWNERSHIP: Envoy owns the pointer, and can be accessed by the module until the filter is
 * destroyed, i.e. envoy_dynamic_module_on_listener_filter_destroy is called.
 */
typedef void* envoy_dynamic_module_type_listener_filter_envoy_ptr;

/**
 * envoy_dynamic_module_type_listener_filter_module_ptr is a pointer to an in-module listener filter
 * corresponding to an Envoy listener filter. The filter is responsible for processing each
 * accepted connection before a Connection object is created.
 *
 * This has 1:1 correspondence with the DynamicModuleListenerFilter class in Envoy.
 *
 * OWNERSHIP: The module is responsible for managing the lifetime of the pointer. The pointer can be
 * released when envoy_dynamic_module_on_listener_filter_destroy is called for the same pointer.
 */
typedef const void* envoy_dynamic_module_type_listener_filter_module_ptr;

/**
 * envoy_dynamic_module_type_listener_filter_scheduler_module_ptr is a raw pointer to the
 * DynamicModuleListenerFilterScheduler class in Envoy.
 *
 * OWNERSHIP: The allocation is done by Envoy but the module is responsible for managing the
 * lifetime of the pointer. Notably, it must be explicitly destroyed by the module
 * when scheduling the listener filter event is done. The creation of this pointer is done by
 * envoy_dynamic_module_callback_listener_filter_scheduler_new and the scheduling and destruction is
 * done by envoy_dynamic_module_callback_listener_filter_scheduler_delete. Since its lifecycle is
 * owned/managed by the module, this has _module_ptr suffix.
 */
typedef void* envoy_dynamic_module_type_listener_filter_scheduler_module_ptr;

/**
 * envoy_dynamic_module_type_listener_filter_config_scheduler_module_ptr is a raw pointer to the
 * DynamicModuleListenerFilterConfigScheduler class in Envoy.
 *
 * OWNERSHIP: The allocation is done by Envoy but the module is responsible for managing the
 * lifetime of the pointer. Notably, it must be explicitly destroyed by the module
 * when scheduling the listener filter config event is done. The creation of this pointer is done by
 * envoy_dynamic_module_callback_listener_filter_config_scheduler_new and the scheduling and
 * destruction is done by envoy_dynamic_module_callback_listener_filter_config_scheduler_delete.
 * Since its lifecycle is owned/managed by the module, this has _module_ptr suffix.
 */
typedef void* envoy_dynamic_module_type_listener_filter_config_scheduler_module_ptr;

/**
 * envoy_dynamic_module_type_on_listener_filter_status represents the status of the filter
 * after processing. This corresponds to `Network::FilterStatus` in envoy/network/filter.h.
 */
typedef enum envoy_dynamic_module_type_on_listener_filter_status {
  // Continue to further filters.
  envoy_dynamic_module_type_on_listener_filter_status_Continue,
  // Stop executing further filters.
  envoy_dynamic_module_type_on_listener_filter_status_StopIteration,
} envoy_dynamic_module_type_on_listener_filter_status;

// =============================================================================
// Listener Filter Event Hooks
// =============================================================================

/**
 * envoy_dynamic_module_on_listener_filter_config_new is called by the main thread when the
 * listener filter config is loaded. The function returns a
 * envoy_dynamic_module_type_listener_filter_config_module_ptr for given name and config.
 *
 * @param filter_config_envoy_ptr is the pointer to the DynamicModuleListenerFilterConfig object
 * for the corresponding config.
 * @param name is the name of the filter owned by Envoy.
 * @param config is the configuration for the module owned by Envoy.
 * @return envoy_dynamic_module_type_listener_filter_config_module_ptr is the pointer to the
 * in-module listener filter configuration. Returning nullptr indicates a failure to initialize the
 * module. When it fails, the filter configuration will be rejected.
 */
envoy_dynamic_module_type_listener_filter_config_module_ptr
envoy_dynamic_module_on_listener_filter_config_new(
    envoy_dynamic_module_type_listener_filter_config_envoy_ptr filter_config_envoy_ptr,
    envoy_dynamic_module_type_envoy_buffer name, envoy_dynamic_module_type_envoy_buffer config);

/**
 * envoy_dynamic_module_on_listener_filter_config_destroy is called when the listener filter
 * configuration is destroyed in Envoy. The module should release any resources associated with
 * the corresponding in-module listener filter configuration.
 *
 * @param filter_config_ptr is a pointer to the in-module listener filter configuration whose
 * corresponding Envoy listener filter configuration is being destroyed.
 */
void envoy_dynamic_module_on_listener_filter_config_destroy(
    envoy_dynamic_module_type_listener_filter_config_module_ptr filter_config_ptr);

/**
 * envoy_dynamic_module_on_listener_filter_new is called when a new listener filter is created for
 * each accepted connection.
 *
 * @param filter_config_ptr is the pointer to the in-module listener filter configuration.
 * @param filter_envoy_ptr is the pointer to the DynamicModuleListenerFilter object of the
 * corresponding listener filter.
 * @return envoy_dynamic_module_type_listener_filter_module_ptr is the pointer to the in-module
 * listener filter. Returning nullptr indicates a failure to initialize the module. When it fails,
 * the connection will be closed.
 */
envoy_dynamic_module_type_listener_filter_module_ptr envoy_dynamic_module_on_listener_filter_new(
    envoy_dynamic_module_type_listener_filter_config_module_ptr filter_config_ptr,
    envoy_dynamic_module_type_listener_filter_envoy_ptr filter_envoy_ptr);

/**
 * envoy_dynamic_module_on_listener_filter_on_accept is called when a new connection is accepted,
 * but BEFORE a Connection object is created. This is the first callback for each connection.
 *
 * @param filter_envoy_ptr is the pointer to the DynamicModuleListenerFilter object.
 * @param filter_module_ptr is the pointer to the in-module listener filter.
 * @return envoy_dynamic_module_type_on_listener_filter_status is the status of the filter.
 * Continue means further filters should be invoked, StopIteration means the filter needs more
 * data or is waiting for an async operation.
 */
envoy_dynamic_module_type_on_listener_filter_status
envoy_dynamic_module_on_listener_filter_on_accept(
    envoy_dynamic_module_type_listener_filter_envoy_ptr filter_envoy_ptr,
    envoy_dynamic_module_type_listener_filter_module_ptr filter_module_ptr);

/**
 * envoy_dynamic_module_on_listener_filter_on_data is called when data is available for inspection.
 * The data is peek-based, meaning it stays in the buffer for subsequent filters.
 *
 * @param filter_envoy_ptr is the pointer to the DynamicModuleListenerFilter object.
 * @param filter_module_ptr is the pointer to the in-module listener filter.
 * @param data_length is the total length of the available data buffer.
 * @return envoy_dynamic_module_type_on_listener_filter_status is the status of the filter.
 */
envoy_dynamic_module_type_on_listener_filter_status envoy_dynamic_module_on_listener_filter_on_data(
    envoy_dynamic_module_type_listener_filter_envoy_ptr filter_envoy_ptr,
    envoy_dynamic_module_type_listener_filter_module_ptr filter_module_ptr, size_t data_length);

/**
 * envoy_dynamic_module_on_listener_filter_on_close is called when the socket is closed.
 * Only the current filter that has stopped filter chain iteration will get this callback.
 *
 * @param filter_envoy_ptr is the pointer to the DynamicModuleListenerFilter object.
 * @param filter_module_ptr is the pointer to the in-module listener filter.
 */
void envoy_dynamic_module_on_listener_filter_on_close(
    envoy_dynamic_module_type_listener_filter_envoy_ptr filter_envoy_ptr,
    envoy_dynamic_module_type_listener_filter_module_ptr filter_module_ptr);

/**
 * envoy_dynamic_module_on_listener_filter_get_max_read_bytes is called to query the maximum
 * number of bytes the filter wants to inspect from the connection.
 *
 * This is called frequently and should be a fast operation.
 *
 * @param filter_envoy_ptr is the pointer to the DynamicModuleListenerFilter object.
 * @param filter_module_ptr is the pointer to the in-module listener filter.
 * @return the maximum number of bytes to read. 0 means the filter does not need any data.
 */
size_t envoy_dynamic_module_on_listener_filter_get_max_read_bytes(
    envoy_dynamic_module_type_listener_filter_envoy_ptr filter_envoy_ptr,
    envoy_dynamic_module_type_listener_filter_module_ptr filter_module_ptr);

/**
 * envoy_dynamic_module_on_listener_filter_destroy is called when the listener filter is destroyed
 * for each accepted connection.
 *
 * @param filter_module_ptr is the pointer to the in-module listener filter.
 */
void envoy_dynamic_module_on_listener_filter_destroy(
    envoy_dynamic_module_type_listener_filter_module_ptr filter_module_ptr);

/**
 * envoy_dynamic_module_on_listener_filter_scheduled is called when the listener filter is scheduled
 * to be executed on the worker thread where the listener filter is running with
 * envoy_dynamic_module_callback_listener_filter_scheduler_commit callback.
 *
 * @param filter_envoy_ptr is the pointer to the DynamicModuleListenerFilter object of the
 * corresponding listener filter.
 * @param filter_module_ptr is the pointer to the in-module listener filter created by
 * envoy_dynamic_module_on_listener_filter_new.
 * @param event_id is the ID of the event passed to
 * envoy_dynamic_module_callback_listener_filter_scheduler_commit.
 */
void envoy_dynamic_module_on_listener_filter_scheduled(
    envoy_dynamic_module_type_listener_filter_envoy_ptr filter_envoy_ptr,
    envoy_dynamic_module_type_listener_filter_module_ptr filter_module_ptr, uint64_t event_id);

/**
 * envoy_dynamic_module_on_listener_filter_config_scheduled is called when the listener filter
 * configuration is scheduled to be executed on the main thread with
 * envoy_dynamic_module_callback_listener_filter_config_scheduler_commit callback.
 *
 * @param filter_config_envoy_ptr is the pointer to the DynamicModuleListenerFilterConfig object.
 * @param filter_config_module_ptr is the pointer to the in-module listener filter config created by
 * envoy_dynamic_module_on_listener_filter_config_new.
 * @param event_id is the ID of the event passed to
 * envoy_dynamic_module_callback_listener_filter_config_scheduler_commit.
 */
void envoy_dynamic_module_on_listener_filter_config_scheduled(
    envoy_dynamic_module_type_listener_filter_config_envoy_ptr filter_config_envoy_ptr,
    envoy_dynamic_module_type_listener_filter_config_module_ptr filter_config_module_ptr,
    uint64_t event_id);

/**
 * envoy_dynamic_module_on_listener_filter_http_callout_done is called when the HTTP callout
 * response is received initiated by a listener filter.
 *
 * @param filter_envoy_ptr is the pointer to the DynamicModuleListenerFilter object of the
 * corresponding listener filter.
 * @param filter_module_ptr is the pointer to the in-module listener filter created by
 * envoy_dynamic_module_on_listener_filter_new.
 * @param callout_id is the ID of the callout. This is used to differentiate between multiple
 * calls.
 * @param result is the result of the callout.
 * @param headers is the headers of the response.
 * @param headers_size is the size of the headers.
 * @param body_chunks is the body of the response.
 * @param body_chunks_size is the size of the body.
 *
 * headers and body_chunks are owned by Envoy, and they are guaranteed to be valid until the end of
 * this event hook. They may be null if the callout fails or the response is empty.
 */
void envoy_dynamic_module_on_listener_filter_http_callout_done(
    envoy_dynamic_module_type_listener_filter_envoy_ptr filter_envoy_ptr,
    envoy_dynamic_module_type_listener_filter_module_ptr filter_module_ptr, uint64_t callout_id,
    envoy_dynamic_module_type_http_callout_result result,
    envoy_dynamic_module_type_envoy_http_header* headers, size_t headers_size,
    envoy_dynamic_module_type_envoy_buffer* body_chunks, size_t body_chunks_size);

// =============================================================================
// Listener Filter Callbacks
// =============================================================================
//
// Callbacks are functions implemented by Envoy that can be called by the module to interact with
// Envoy. The name of a callback must be prefixed with "envoy_dynamic_module_callback_".

// ---------------------------- Buffer Operations -----------------------------

/**
 * envoy_dynamic_module_callback_listener_filter_get_buffer_chunk is called by the module to
 * get the current data buffer as a single chunk. This is only valid during the
 * envoy_dynamic_module_on_listener_filter_on_data callback.
 *
 * @param filter_envoy_ptr is the pointer to the DynamicModuleListenerFilter object.
 * @param chunk_out is the output pointer to the buffer chunk owned by Envoy.
 * @return true if the buffer is available, false otherwise.
 *
 * The returned data is owned by Envoy and valid until the end of the callback.
 */
bool envoy_dynamic_module_callback_listener_filter_get_buffer_chunk(
    envoy_dynamic_module_type_listener_filter_envoy_ptr filter_envoy_ptr,
    envoy_dynamic_module_type_envoy_buffer* chunk_out);

/**
 * envoy_dynamic_module_callback_listener_filter_drain_buffer is called by the module to drain
 * bytes from the beginning of the buffer.
 *
 * @param filter_envoy_ptr is the pointer to the DynamicModuleListenerFilter object.
 * @param length is the number of bytes to drain from the beginning of the buffer.
 * @return true if the drain was successful, false otherwise.
 */
bool envoy_dynamic_module_callback_listener_filter_drain_buffer(
    envoy_dynamic_module_type_listener_filter_envoy_ptr filter_envoy_ptr, size_t length);

// --------------------- Socket Property Setters (Protocol Detection) -----------

/**
 * envoy_dynamic_module_callback_listener_filter_set_detected_transport_protocol is called by the
 * module to set the detected transport protocol (e.g., "tls", "raw_buffer").
 *
 * @param filter_envoy_ptr is the pointer to the DynamicModuleListenerFilter object.
 * @param protocol is the protocol string owned by the module.
 */
void envoy_dynamic_module_callback_listener_filter_set_detected_transport_protocol(
    envoy_dynamic_module_type_listener_filter_envoy_ptr filter_envoy_ptr,
    envoy_dynamic_module_type_module_buffer protocol);

/**
 * envoy_dynamic_module_callback_listener_filter_set_requested_server_name is called by the module
 * to set the requested server name (SNI).
 *
 * @param filter_envoy_ptr is the pointer to the DynamicModuleListenerFilter object.
 * @param name is the server name string owned by the module.
 */
void envoy_dynamic_module_callback_listener_filter_set_requested_server_name(
    envoy_dynamic_module_type_listener_filter_envoy_ptr filter_envoy_ptr,
    envoy_dynamic_module_type_module_buffer name);

/**
 * envoy_dynamic_module_callback_listener_filter_set_requested_application_protocols is called by
 * the module to set the requested application protocols (ALPN).
 *
 * @param filter_envoy_ptr is the pointer to the DynamicModuleListenerFilter object.
 * @param protocols is an array of protocol name buffers owned by the module.
 * @param protocols_count is the number of protocols.
 */
void envoy_dynamic_module_callback_listener_filter_set_requested_application_protocols(
    envoy_dynamic_module_type_listener_filter_envoy_ptr filter_envoy_ptr,
    envoy_dynamic_module_type_module_buffer* protocols, size_t protocols_count);

/**
 * `envoy_dynamic_module_callback_listener_filter_set_ja3_hash` is called by the module to set the
 * `JA3` fingerprint hash.
 *
 * @param filter_envoy_ptr is the pointer to the DynamicModuleListenerFilter object.
 * @param hash is the hash string owned by the module.
 */
void envoy_dynamic_module_callback_listener_filter_set_ja3_hash(
    envoy_dynamic_module_type_listener_filter_envoy_ptr filter_envoy_ptr,
    envoy_dynamic_module_type_module_buffer hash);

/**
 * `envoy_dynamic_module_callback_listener_filter_set_ja4_hash` is called by the module to set the
 * `JA4` fingerprint hash.
 *
 * @param filter_envoy_ptr is the pointer to the DynamicModuleListenerFilter object.
 * @param hash is the hash string owned by the module.
 */
void envoy_dynamic_module_callback_listener_filter_set_ja4_hash(
    envoy_dynamic_module_type_listener_filter_envoy_ptr filter_envoy_ptr,
    envoy_dynamic_module_type_module_buffer hash);

// --------------------- Socket Property Getters (Protocol Detection & SSL) ----

/**
 * envoy_dynamic_module_callback_listener_filter_get_requested_server_name is called by the module
 * to get the requested server name (SNI) from the connection socket. This returns the value
 * previously set by a listener filter (e.g., TLS inspector).
 *
 * @param filter_envoy_ptr is the pointer to the DynamicModuleListenerFilter object.
 * @param result_out is the output buffer where the SNI string owned by Envoy will be stored.
 * @return true if SNI is available, false otherwise.
 */
bool envoy_dynamic_module_callback_listener_filter_get_requested_server_name(
    envoy_dynamic_module_type_listener_filter_envoy_ptr filter_envoy_ptr,
    envoy_dynamic_module_type_envoy_buffer* result_out);

/**
 * envoy_dynamic_module_callback_listener_filter_get_detected_transport_protocol is called by the
 * module to get the detected transport protocol (e.g., "tls", "raw_buffer") from the connection
 * socket.
 *
 * @param filter_envoy_ptr is the pointer to the DynamicModuleListenerFilter object.
 * @param result_out is the output buffer where the protocol string owned by Envoy will be stored.
 * @return true if the transport protocol is available, false otherwise.
 */
bool envoy_dynamic_module_callback_listener_filter_get_detected_transport_protocol(
    envoy_dynamic_module_type_listener_filter_envoy_ptr filter_envoy_ptr,
    envoy_dynamic_module_type_envoy_buffer* result_out);

/**
 * envoy_dynamic_module_callback_listener_filter_get_requested_application_protocols_size is called
 * by the module to get the count of requested application protocols (ALPN) from the connection
 * socket.
 *
 * @param filter_envoy_ptr is the pointer to the DynamicModuleListenerFilter object.
 * @return the count of application protocols, or 0 if none are available.
 */
size_t envoy_dynamic_module_callback_listener_filter_get_requested_application_protocols_size(
    envoy_dynamic_module_type_listener_filter_envoy_ptr filter_envoy_ptr);

/**
 * envoy_dynamic_module_callback_listener_filter_get_requested_application_protocols is called by
 * the module to get the requested application protocols (ALPN) from the connection socket. The
 * module should first call get_requested_application_protocols_size to get the count and allocate
 * the array.
 *
 * @param filter_envoy_ptr is the pointer to the DynamicModuleListenerFilter object.
 * @param protocols_out is a pre-allocated array owned by the module where Envoy will populate the
 *   protocol strings. The module must allocate this array with at least the size returned by
 *   get_requested_application_protocols_size.
 * @return true if the protocols were populated successfully, false otherwise.
 */
bool envoy_dynamic_module_callback_listener_filter_get_requested_application_protocols(
    envoy_dynamic_module_type_listener_filter_envoy_ptr filter_envoy_ptr,
    envoy_dynamic_module_type_envoy_buffer* protocols_out);

/**
 * `envoy_dynamic_module_callback_listener_filter_get_ja3_hash` is called by the module to get the
 * `JA3` fingerprint hash from the connection socket.
 *
 * @param filter_envoy_ptr is the pointer to the DynamicModuleListenerFilter object.
 * @param result_out is the output buffer where the `JA3` hash string owned by Envoy will be stored.
 * @return true if the `JA3` hash is available, false otherwise.
 */
bool envoy_dynamic_module_callback_listener_filter_get_ja3_hash(
    envoy_dynamic_module_type_listener_filter_envoy_ptr filter_envoy_ptr,
    envoy_dynamic_module_type_envoy_buffer* result_out);

/**
 * `envoy_dynamic_module_callback_listener_filter_get_ja4_hash` is called by the module to get the
 * `JA4` fingerprint hash from the connection socket.
 *
 * @param filter_envoy_ptr is the pointer to the DynamicModuleListenerFilter object.
 * @param result_out is the output buffer where the `JA4` hash string owned by Envoy will be stored.
 * @return true if the `JA4` hash is available, false otherwise.
 */
bool envoy_dynamic_module_callback_listener_filter_get_ja4_hash(
    envoy_dynamic_module_type_listener_filter_envoy_ptr filter_envoy_ptr,
    envoy_dynamic_module_type_envoy_buffer* result_out);

/**
 * envoy_dynamic_module_callback_listener_filter_is_ssl is called by the module to check if the
 * connection has SSL/TLS information available on the socket.
 *
 * @param filter_envoy_ptr is the pointer to the DynamicModuleListenerFilter object.
 * @return true if SSL/TLS connection information is available, false otherwise.
 */
bool envoy_dynamic_module_callback_listener_filter_is_ssl(
    envoy_dynamic_module_type_listener_filter_envoy_ptr filter_envoy_ptr);

/**
 * envoy_dynamic_module_callback_listener_filter_get_ssl_uri_sans_size is called by the module to
 * get the count of URI Subject Alternative Names from the peer certificate.
 *
 * @param filter_envoy_ptr is the pointer to the DynamicModuleListenerFilter object.
 * @return the count of URI SANs, or 0 if SSL is not available.
 */
size_t envoy_dynamic_module_callback_listener_filter_get_ssl_uri_sans_size(
    envoy_dynamic_module_type_listener_filter_envoy_ptr filter_envoy_ptr);

/**
 * envoy_dynamic_module_callback_listener_filter_get_ssl_uri_sans is called by the module to get
 * the URI Subject Alternative Names from the peer certificate. The module should first call
 * get_ssl_uri_sans_size to get the count and allocate the array.
 *
 * @param filter_envoy_ptr is the pointer to the DynamicModuleListenerFilter object.
 * @param sans_out is a pre-allocated array owned by the module where Envoy will populate the SANs.
 *   The module must allocate this array with at least the size returned by get_ssl_uri_sans_size.
 * @return true if the SANs were populated successfully, false if SSL is not available.
 */
bool envoy_dynamic_module_callback_listener_filter_get_ssl_uri_sans(
    envoy_dynamic_module_type_listener_filter_envoy_ptr filter_envoy_ptr,
    envoy_dynamic_module_type_envoy_buffer* sans_out);

/**
 * envoy_dynamic_module_callback_listener_filter_get_ssl_dns_sans_size is called by the module to
 * get the count of DNS Subject Alternative Names from the peer certificate.
 *
 * @param filter_envoy_ptr is the pointer to the DynamicModuleListenerFilter object.
 * @return the count of DNS SANs, or 0 if SSL is not available.
 */
size_t envoy_dynamic_module_callback_listener_filter_get_ssl_dns_sans_size(
    envoy_dynamic_module_type_listener_filter_envoy_ptr filter_envoy_ptr);

/**
 * envoy_dynamic_module_callback_listener_filter_get_ssl_dns_sans is called by the module to get
 * the DNS Subject Alternative Names from the peer certificate. The module should first call
 * get_ssl_dns_sans_size to get the count and allocate the array.
 *
 * @param filter_envoy_ptr is the pointer to the DynamicModuleListenerFilter object.
 * @param sans_out is a pre-allocated array owned by the module where Envoy will populate the SANs.
 *   The module must allocate this array with at least the size returned by get_ssl_dns_sans_size.
 * @return true if the SANs were populated successfully, false if SSL is not available.
 */
bool envoy_dynamic_module_callback_listener_filter_get_ssl_dns_sans(
    envoy_dynamic_module_type_listener_filter_envoy_ptr filter_envoy_ptr,
    envoy_dynamic_module_type_envoy_buffer* sans_out);

/**
 * envoy_dynamic_module_callback_listener_filter_get_ssl_subject is called by the module to get
 * the subject from the peer certificate.
 *
 * @param filter_envoy_ptr is the pointer to the DynamicModuleListenerFilter object.
 * @param result_out is the output buffer where the subject owned by Envoy will be stored.
 * @return true if SSL is available, false otherwise.
 */
bool envoy_dynamic_module_callback_listener_filter_get_ssl_subject(
    envoy_dynamic_module_type_listener_filter_envoy_ptr filter_envoy_ptr,
    envoy_dynamic_module_type_envoy_buffer* result_out);

// --------------------------- Address Operations -----------------------------

/**
 * envoy_dynamic_module_callback_listener_filter_get_remote_address is called by the module to get
 * the remote (client) address.
 *
 * @param filter_envoy_ptr is the pointer to the DynamicModuleListenerFilter object.
 * @param address_out is the output buffer where the address owned by Envoy will be stored.
 * @param port_out is the output pointer to the port number.
 * @return true if the address was found and is an IP address, false otherwise.
 */
bool envoy_dynamic_module_callback_listener_filter_get_remote_address(
    envoy_dynamic_module_type_listener_filter_envoy_ptr filter_envoy_ptr,
    envoy_dynamic_module_type_envoy_buffer* address_out, uint32_t* port_out);

/**
 * envoy_dynamic_module_callback_listener_filter_get_direct_remote_address is called by the module
 * to get the direct remote address which is the peer address before any listener filter
 * modification.
 *
 * @param filter_envoy_ptr is the pointer to the DynamicModuleListenerFilter object.
 * @param address_out is the output buffer where the address owned by Envoy will be stored.
 * @param port_out is the output pointer to the port number.
 * @return true if the address was found and is an IP address, false otherwise.
 */
bool envoy_dynamic_module_callback_listener_filter_get_direct_remote_address(
    envoy_dynamic_module_type_listener_filter_envoy_ptr filter_envoy_ptr,
    envoy_dynamic_module_type_envoy_buffer* address_out, uint32_t* port_out);

/**
 * envoy_dynamic_module_callback_listener_filter_get_local_address is called by the module to get
 * the local address.
 *
 * @param filter_envoy_ptr is the pointer to the DynamicModuleListenerFilter object.
 * @param address_out is the output buffer where the address owned by Envoy will be stored.
 * @param port_out is the output pointer to the port number.
 * @return true if the address was found and is an IP address, false otherwise.
 */
bool envoy_dynamic_module_callback_listener_filter_get_local_address(
    envoy_dynamic_module_type_listener_filter_envoy_ptr filter_envoy_ptr,
    envoy_dynamic_module_type_envoy_buffer* address_out, uint32_t* port_out);

/**
 * envoy_dynamic_module_callback_listener_filter_get_direct_local_address is called by the module to
 * get the direct local address (the listener address before any restoration).
 *
 * @param filter_envoy_ptr is the pointer to the DynamicModuleListenerFilter object.
 * @param address_out is the output buffer where the address owned by Envoy will be stored.
 * @param port_out is the output pointer to the port number.
 * @return true if the address was found and is an IP address, false otherwise.
 */
bool envoy_dynamic_module_callback_listener_filter_get_direct_local_address(
    envoy_dynamic_module_type_listener_filter_envoy_ptr filter_envoy_ptr,
    envoy_dynamic_module_type_envoy_buffer* address_out, uint32_t* port_out);

// ---------------------- HTTP filter scheduler callbacks ------------------------
/**
 * envoy_dynamic_module_callback_listener_filter_get_original_dst is called by the module to get the
 * original destination address obtained from the platform (e.g., iptables redirect).
 *
 * @param filter_envoy_ptr is the pointer to the DynamicModuleListenerFilter object.
 * @param address_out is the output buffer where the address owned by Envoy will be stored.
 * @param port_out is the output pointer to the port number.
 * @return true if the original destination address was found and is an IP address, false otherwise.
 */
bool envoy_dynamic_module_callback_listener_filter_get_original_dst(
    envoy_dynamic_module_type_listener_filter_envoy_ptr filter_envoy_ptr,
    envoy_dynamic_module_type_envoy_buffer* address_out, uint32_t* port_out);

/**
 * envoy_dynamic_module_callback_listener_filter_get_address_type is called by the module to get the
 * socket address type.
 *
 * @param filter_envoy_ptr is the pointer to the DynamicModuleListenerFilter object.
 * @return the address type for the current socket.
 */
envoy_dynamic_module_type_address_type
envoy_dynamic_module_callback_listener_filter_get_address_type(
    envoy_dynamic_module_type_listener_filter_envoy_ptr filter_envoy_ptr);

/**
 * envoy_dynamic_module_callback_listener_filter_is_local_address_restored is called by the module
 * to check whether the local address has been restored to a value different from the listener
 * address.
 *
 * @param filter_envoy_ptr is the pointer to the DynamicModuleListenerFilter object.
 * @return true if the local address has been restored, false otherwise.
 */
bool envoy_dynamic_module_callback_listener_filter_is_local_address_restored(
    envoy_dynamic_module_type_listener_filter_envoy_ptr filter_envoy_ptr);

/**
 * envoy_dynamic_module_callback_listener_filter_set_remote_address is called by the module to set
 * the remote address (for proxy protocol parsing).
 *
 * @param filter_envoy_ptr is the pointer to the DynamicModuleListenerFilter object.
 * @param address is the address string owned by the module.
 * @param port is the port number.
 * @param is_ipv6 true if the address is IPv6, false for IPv4.
 * @return true if successful, false otherwise.
 */
bool envoy_dynamic_module_callback_listener_filter_set_remote_address(
    envoy_dynamic_module_type_listener_filter_envoy_ptr filter_envoy_ptr,
    envoy_dynamic_module_type_module_buffer address, uint32_t port, bool is_ipv6);

/**
 * envoy_dynamic_module_callback_listener_filter_restore_local_address is called by the module to
 * restore the local address (for original destination or proxy protocol).
 *
 * @param filter_envoy_ptr is the pointer to the DynamicModuleListenerFilter object.
 * @param address is the address string owned by the module.
 * @param port is the port number.
 * @param is_ipv6 true if the address is IPv6, false for IPv4.
 * @return true if successful, false otherwise.
 */
bool envoy_dynamic_module_callback_listener_filter_restore_local_address(
    envoy_dynamic_module_type_listener_filter_envoy_ptr filter_envoy_ptr,
    envoy_dynamic_module_type_module_buffer address, uint32_t port, bool is_ipv6);

// ---------------------- Filter Chain Control ---------------------------------

/**
 * envoy_dynamic_module_callback_listener_filter_continue_filter_chain is called by the module to
 * continue the filter chain after returning StopIteration.
 *
 * @param filter_envoy_ptr is the pointer to the DynamicModuleListenerFilter object.
 * @param success is true if the filter execution was successful, false if the connection should
 * be closed.
 */
void envoy_dynamic_module_callback_listener_filter_continue_filter_chain(
    envoy_dynamic_module_type_listener_filter_envoy_ptr filter_envoy_ptr, bool success);

/**
 * envoy_dynamic_module_callback_listener_filter_use_original_dst is called by the module to control
 * whether the listener should use the original destination for filter chain matching.
 *
 * @param filter_envoy_ptr is the pointer to the DynamicModuleListenerFilter object.
 * @param use_original_dst indicates whether to enable original destination handling.
 */
void envoy_dynamic_module_callback_listener_filter_use_original_dst(
    envoy_dynamic_module_type_listener_filter_envoy_ptr filter_envoy_ptr, bool use_original_dst);

/**
 * envoy_dynamic_module_callback_listener_filter_close_socket is called by the module to close
 * the socket immediately. If details is non-empty, the termination reason is set on the
 * connection's stream info before closing.
 *
 * @param filter_envoy_ptr is the pointer to the DynamicModuleListenerFilter object.
 * @param details is the optional termination reason string owned by the module. Can be empty.
 */
void envoy_dynamic_module_callback_listener_filter_close_socket(
    envoy_dynamic_module_type_listener_filter_envoy_ptr filter_envoy_ptr,
    envoy_dynamic_module_type_module_buffer details);

/**
 * envoy_dynamic_module_callback_listener_filter_write_to_socket is called by the module to write
 * data directly to the raw socket. This is useful for protocol negotiation at the listener filter
 * level, such as writing SSL support responses in Postgres or MySQL handshake packets.
 *
 * @param filter_envoy_ptr is the pointer to the DynamicModuleListenerFilter object.
 * @param data is the data to write owned by the module.
 * @return the number of bytes written, or -1 if the write failed or callbacks are not available.
 */
int64_t envoy_dynamic_module_callback_listener_filter_write_to_socket(
    envoy_dynamic_module_type_listener_filter_envoy_ptr filter_envoy_ptr,
    envoy_dynamic_module_type_module_buffer data);

// ---------------------- Socket Option Callbacks ------------------------------

/**
 * envoy_dynamic_module_callback_listener_filter_get_socket_fd is called by the module to get the
 * raw socket file descriptor for advanced socket manipulations.
 *
 * @param filter_envoy_ptr is the pointer to the DynamicModuleListenerFilter object.
 * @return the socket file descriptor, or -1 if the socket is not available.
 */
int64_t envoy_dynamic_module_callback_listener_filter_get_socket_fd(
    envoy_dynamic_module_type_listener_filter_envoy_ptr filter_envoy_ptr);

/**
 * envoy_dynamic_module_callback_listener_filter_set_socket_option_int sets an integer socket option
 * on the accepted socket (@see man 2 setsockopt).
 *
 * @param filter_envoy_ptr is the pointer to the DynamicModuleListenerFilter object.
 * @param level is the socket option level (e.g., SOL_SOCKET, IPPROTO_TCP).
 * @param name is the socket option name (e.g., SO_KEEPALIVE, TCP_NODELAY).
 * @param value is the integer value for the socket option.
 * @return true if the socket option was set successfully, false otherwise.
 */
bool envoy_dynamic_module_callback_listener_filter_set_socket_option_int(
    envoy_dynamic_module_type_listener_filter_envoy_ptr filter_envoy_ptr, int64_t level,
    int64_t name, int64_t value);

/**
 * envoy_dynamic_module_callback_listener_filter_set_socket_option_bytes sets a bytes socket option
 * on the accepted socket (@see man 2 setsockopt).
 *
 * @param filter_envoy_ptr is the pointer to the DynamicModuleListenerFilter object.
 * @param level is the socket option level (e.g., SOL_SOCKET, IPPROTO_TCP).
 * @param name is the socket option name (e.g., SO_KEEPALIVE, TCP_NODELAY).
 * @param value is the byte buffer value for the socket option.
 * @return true if the socket option was set successfully, false otherwise.
 */
bool envoy_dynamic_module_callback_listener_filter_set_socket_option_bytes(
    envoy_dynamic_module_type_listener_filter_envoy_ptr filter_envoy_ptr, int64_t level,
    int64_t name, envoy_dynamic_module_type_module_buffer value);

/**
 * envoy_dynamic_module_callback_listener_filter_get_socket_option_int retrieves an integer socket
 * option value from the accepted socket (@see man 2 getsockopt). This reads the actual value from
 * the socket, including options set by other filters or the system.
 *
 * @param filter_envoy_ptr is the pointer to the DynamicModuleListenerFilter object.
 * @param level is the socket option level (e.g., SOL_SOCKET, IPPROTO_TCP).
 * @param name is the socket option name (e.g., SO_KEEPALIVE, TCP_NODELAY).
 * @param value_out is the pointer to store the retrieved integer value.
 * @return true if the option was retrieved successfully, false otherwise.
 */
bool envoy_dynamic_module_callback_listener_filter_get_socket_option_int(
    envoy_dynamic_module_type_listener_filter_envoy_ptr filter_envoy_ptr, int64_t level,
    int64_t name, int64_t* value_out);

/**
 * envoy_dynamic_module_callback_listener_filter_get_socket_option_bytes retrieves a bytes socket
 * option value from the accepted socket (@see man 2 getsockopt). This reads the actual value from
 * the socket, including options set by other filters or the system.
 *
 * @param filter_envoy_ptr is the pointer to the DynamicModuleListenerFilter object.
 * @param level is the socket option level (e.g., SOL_SOCKET, IPPROTO_TCP).
 * @param name is the socket option name (e.g., SO_KEEPALIVE, TCP_NODELAY).
 * @param value_out is the pointer to store the retrieved buffer. The module should pre-allocate the
 * buffer with sufficient size before calling this function.
 * @param value_size is the size of the pre-allocated buffer.
 * @param actual_size_out is the pointer to store the actual size of the option value.
 * @return true if the option was retrieved successfully, false otherwise.
 */
bool envoy_dynamic_module_callback_listener_filter_get_socket_option_bytes(
    envoy_dynamic_module_type_listener_filter_envoy_ptr filter_envoy_ptr, int64_t level,
    int64_t name, char* value_out, size_t value_size, size_t* actual_size_out);

// ------------------------- Filter State Operations ---------------------------

/**
 * envoy_dynamic_module_callback_listener_filter_set_filter_state is called by the module to
 * set a string value in filter state with Connection life span.
 *
 * @param filter_envoy_ptr is the pointer to the DynamicModuleListenerFilter object.
 * @param key is the key string owned by the module.
 * @param value is the value string owned by the module.
 * @return true if the operation was successful, false otherwise.
 */
bool envoy_dynamic_module_callback_listener_filter_set_filter_state(
    envoy_dynamic_module_type_listener_filter_envoy_ptr filter_envoy_ptr,
    envoy_dynamic_module_type_module_buffer key, envoy_dynamic_module_type_module_buffer value);

/**
 * envoy_dynamic_module_callback_listener_filter_get_filter_state is called by the module to
 * get a string value from filter state.
 *
 * @param filter_envoy_ptr is the pointer to the DynamicModuleListenerFilter object.
 * @param key is the key string owned by the module.
 * @param value_out is the output buffer where the value owned by Envoy will be stored.
 * @return true if the value was found, false otherwise.
 */
bool envoy_dynamic_module_callback_listener_filter_get_filter_state(
    envoy_dynamic_module_type_listener_filter_envoy_ptr filter_envoy_ptr,
    envoy_dynamic_module_type_module_buffer key, envoy_dynamic_module_type_envoy_buffer* value_out);

// ------------------------- Stream Info Operations -----------------------------

/**
 * envoy_dynamic_module_callback_listener_filter_set_downstream_transport_failure_reason is called
 * by the module to set the downstream transport failure reason for logging and debugging.
 *
 * @param filter_envoy_ptr is the pointer to the DynamicModuleListenerFilter object.
 * @param reason is the reason string owned by the module.
 */
void envoy_dynamic_module_callback_listener_filter_set_downstream_transport_failure_reason(
    envoy_dynamic_module_type_listener_filter_envoy_ptr filter_envoy_ptr,
    envoy_dynamic_module_type_module_buffer reason);

/**
 * envoy_dynamic_module_callback_listener_filter_get_connection_start_time_ms is called by the
 * module to obtain the connection start time in milliseconds since Unix epoch.
 *
 * @param filter_envoy_ptr is the pointer to the DynamicModuleListenerFilter object.
 * @return the start time in milliseconds since Unix epoch.
 */
uint64_t envoy_dynamic_module_callback_listener_filter_get_connection_start_time_ms(
    envoy_dynamic_module_type_listener_filter_envoy_ptr filter_envoy_ptr);

/**
 * envoy_dynamic_module_callback_listener_filter_get_dynamic_metadata_string is called by the
 * module to retrieve a string-typed dynamic metadata value.
 *
 * @param filter_envoy_ptr is the pointer to the DynamicModuleListenerFilter object.
 * @param filter_namespace is the namespace of the metadata.
 * @param key is the key of the metadata field.
 * @param value_out is the pointer to write the retrieved value. If the metadata is not found or is
 * not a string type, value_out->ptr will be set to nullptr and value_out->length will be 0.
 * @return true if the metadata was found and is a string type, false otherwise.
 */
bool envoy_dynamic_module_callback_listener_filter_get_dynamic_metadata_string(
    envoy_dynamic_module_type_listener_filter_envoy_ptr filter_envoy_ptr,
    envoy_dynamic_module_type_module_buffer filter_namespace,
    envoy_dynamic_module_type_module_buffer key, envoy_dynamic_module_type_envoy_buffer* value_out);

/**
 * envoy_dynamic_module_callback_listener_filter_set_dynamic_metadata_string is called by the
 * module to set a string-typed dynamic metadata value. If the metadata is existing, it will be
 * overwritten.
 *
 * @param filter_envoy_ptr is the pointer to the DynamicModuleListenerFilter object.
 * @param filter_namespace is the namespace of the metadata.
 * @param key is the key of the metadata field.
 * @param value is the string value to set.
 */
void envoy_dynamic_module_callback_listener_filter_set_dynamic_metadata_string(
    envoy_dynamic_module_type_listener_filter_envoy_ptr filter_envoy_ptr,
    envoy_dynamic_module_type_module_buffer filter_namespace,
    envoy_dynamic_module_type_module_buffer key, envoy_dynamic_module_type_module_buffer value);

/**
 * envoy_dynamic_module_callback_listener_filter_get_dynamic_metadata_number is called by the
 * module to retrieve a number-typed dynamic metadata value.
 *
 * @param filter_envoy_ptr is the pointer to the DynamicModuleListenerFilter object.
 * @param filter_namespace is the namespace of the metadata.
 * @param key is the key of the metadata field.
 * @param result is the output pointer to the number value of the dynamic metadata.
 * @return true if the metadata was found and is a number type, false otherwise.
 */
bool envoy_dynamic_module_callback_listener_filter_get_dynamic_metadata_number(
    envoy_dynamic_module_type_listener_filter_envoy_ptr filter_envoy_ptr,
    envoy_dynamic_module_type_module_buffer filter_namespace,
    envoy_dynamic_module_type_module_buffer key, double* result);

/**
 * envoy_dynamic_module_callback_listener_filter_set_dynamic_metadata_number is called by the
 * module to set a number-typed dynamic metadata value. If the metadata is existing, it will be
 * overwritten.
 *
 * @param filter_envoy_ptr is the pointer to the DynamicModuleListenerFilter object.
 * @param filter_namespace is the namespace of the metadata.
 * @param key is the key of the metadata field.
 * @param value is the number value to set.
 */
void envoy_dynamic_module_callback_listener_filter_set_dynamic_metadata_number(
    envoy_dynamic_module_type_listener_filter_envoy_ptr filter_envoy_ptr,
    envoy_dynamic_module_type_module_buffer filter_namespace,
    envoy_dynamic_module_type_module_buffer key, double value);

/**
 * envoy_dynamic_module_callback_listener_filter_max_read_bytes is called by the
 * module to determine the maximum number of bytes to read from the socket.
 *
 * @param filter_envoy_ptr is the pointer to the DynamicModuleListenerFilter object.
 * @return the maximum number of bytes to read.
 */
size_t envoy_dynamic_module_callback_listener_filter_max_read_bytes(
    envoy_dynamic_module_type_listener_filter_envoy_ptr filter_envoy_ptr);

// ------------------------ Listener Filter Callbacks - Metrics -------------------------

/**
 * envoy_dynamic_module_callback_listener_filter_config_define_counter is called by the module
 * during initialization to create a new Stats::Counter with the given name.
 *
 * @param config_envoy_ptr is the pointer to the DynamicModuleListenerFilterConfig in which the
 * counter will be defined.
 * @param name is the name of the counter to be defined.
 * @param counter_id_ptr where the opaque ID that represents a unique metric will be stored. This
 * can be passed to envoy_dynamic_module_callback_listener_filter_increment_counter together with
 * filter_envoy_ptr.
 * @return the result of the operation.
 */
envoy_dynamic_module_type_metrics_result
envoy_dynamic_module_callback_listener_filter_config_define_counter(
    envoy_dynamic_module_type_listener_filter_config_envoy_ptr config_envoy_ptr,
    envoy_dynamic_module_type_module_buffer name, size_t* counter_id_ptr);

/**
 * envoy_dynamic_module_callback_listener_filter_increment_counter is called by the module to
 * increment a previously defined counter.
 *
 * @param filter_envoy_ptr is the pointer to the DynamicModuleListenerFilter object.
 * @param id is the ID of the counter previously defined using the config.
 * @param value is the value to increment the counter by.
 * @return the result of the operation.
 */
envoy_dynamic_module_type_metrics_result
envoy_dynamic_module_callback_listener_filter_increment_counter(
    envoy_dynamic_module_type_listener_filter_envoy_ptr filter_envoy_ptr, size_t id,
    uint64_t value);

/**
 * envoy_dynamic_module_callback_listener_filter_config_define_gauge is called by the module during
 * initialization to create a new Stats::Gauge with the given name.
 *
 * @param config_envoy_ptr is the pointer to the DynamicModuleListenerFilterConfig in which the
 * gauge will be defined.
 * @param name is the name of the gauge to be defined.
 * @param gauge_id_ptr where the opaque ID that represents a unique metric will be stored.
 * @return the result of the operation.
 */
envoy_dynamic_module_type_metrics_result
envoy_dynamic_module_callback_listener_filter_config_define_gauge(
    envoy_dynamic_module_type_listener_filter_config_envoy_ptr config_envoy_ptr,
    envoy_dynamic_module_type_module_buffer name, size_t* gauge_id_ptr);

/**
 * envoy_dynamic_module_callback_listener_filter_set_gauge is called by the module to set the value
 * of a previously defined gauge.
 *
 * @param filter_envoy_ptr is the pointer to the DynamicModuleListenerFilter object.
 * @param id is the ID of the gauge previously defined using the config.
 * @param value is the value to set the gauge to.
 * @return the result of the operation.
 */
envoy_dynamic_module_type_metrics_result envoy_dynamic_module_callback_listener_filter_set_gauge(
    envoy_dynamic_module_type_listener_filter_envoy_ptr filter_envoy_ptr, size_t id,
    uint64_t value);

/**
 * envoy_dynamic_module_callback_listener_filter_increment_gauge is called by the module to increase
 * the value of a previously defined gauge.
 *
 * @param filter_envoy_ptr is the pointer to the DynamicModuleListenerFilter object.
 * @param id is the ID of the gauge previously defined using the config.
 * @param value is the value to increase the gauge by.
 * @return the result of the operation.
 */
envoy_dynamic_module_type_metrics_result
envoy_dynamic_module_callback_listener_filter_increment_gauge(
    envoy_dynamic_module_type_listener_filter_envoy_ptr filter_envoy_ptr, size_t id,
    uint64_t value);

/**
 * envoy_dynamic_module_callback_listener_filter_decrement_gauge is called by the module to decrease
 * the value of a previously defined gauge.
 *
 * @param filter_envoy_ptr is the pointer to the DynamicModuleListenerFilter object.
 * @param id is the ID of the gauge previously defined using the config.
 * @param value is the value to decrease the gauge by.
 * @return the result of the operation.
 */
envoy_dynamic_module_type_metrics_result
envoy_dynamic_module_callback_listener_filter_decrement_gauge(
    envoy_dynamic_module_type_listener_filter_envoy_ptr filter_envoy_ptr, size_t id,
    uint64_t value);

/**
 * envoy_dynamic_module_callback_listener_filter_config_define_histogram is called by the module
 * during initialization to create a new Stats::Histogram with the given name.
 *
 * @param config_envoy_ptr is the pointer to the DynamicModuleListenerFilterConfig in which the
 * histogram will be defined.
 * @param name is the name of the histogram to be defined.
 * @param histogram_id_ptr where the opaque ID that represents a unique metric will be stored.
 * @return the result of the operation.
 */
envoy_dynamic_module_type_metrics_result
envoy_dynamic_module_callback_listener_filter_config_define_histogram(
    envoy_dynamic_module_type_listener_filter_config_envoy_ptr config_envoy_ptr,
    envoy_dynamic_module_type_module_buffer name, size_t* histogram_id_ptr);

/**
 * envoy_dynamic_module_callback_listener_filter_record_histogram_value is called by the module to
 * record a value in a previously defined histogram.
 *
 * @param filter_envoy_ptr is the pointer to the DynamicModuleListenerFilter object.
 * @param id is the ID of the histogram previously defined using the config.
 * @param value is the value to record in the histogram.
 * @return the result of the operation.
 */
envoy_dynamic_module_type_metrics_result
envoy_dynamic_module_callback_listener_filter_record_histogram_value(
    envoy_dynamic_module_type_listener_filter_envoy_ptr filter_envoy_ptr, size_t id,
    uint64_t value);

// ---------------------- Listener Filter Callbacks - HTTP Callout ---------------

/**
 * envoy_dynamic_module_callback_listener_filter_http_callout is called by the module to initiate an
 * HTTP callout. The callout is initiated by the listener filter and the response is received in
 * envoy_dynamic_module_on_listener_filter_http_callout_done.
 *
 * @param filter_envoy_ptr is the pointer to the DynamicModuleListenerFilter object of the
 * corresponding listener filter.
 * @param callout_id_out is a pointer to a variable where the callout ID will be stored. This can be
 * arbitrary and is used to differentiate between multiple calls from the same filter.
 * @param cluster_name is the name of the cluster to which the callout is sent.
 * @param headers is the headers of the request. It must contain :method, :path and host headers.
 * @param headers_size is the size of the headers.
 * @param body is the body of the request.
 * @param timeout_milliseconds is the timeout for the callout in milliseconds.
 * @return envoy_dynamic_module_type_http_callout_init_result is the result of the callout
 * initialization.
 */
envoy_dynamic_module_type_http_callout_init_result
envoy_dynamic_module_callback_listener_filter_http_callout(
    envoy_dynamic_module_type_listener_filter_envoy_ptr filter_envoy_ptr, uint64_t* callout_id_out,
    envoy_dynamic_module_type_module_buffer cluster_name,
    envoy_dynamic_module_type_module_http_header* headers, size_t headers_size,
    envoy_dynamic_module_type_module_buffer body, uint64_t timeout_milliseconds);

// ---------------------- Listener filter scheduler callbacks -----------------

/**
 * envoy_dynamic_module_callback_listener_filter_scheduler_new is called by the module to create a
 * new listener filter scheduler. The scheduler is used to dispatch listener filter operations from
 * any thread including the ones managed by the module.
 *
 * @param filter_envoy_ptr is the pointer to the DynamicModuleListenerFilter object of the
 * corresponding listener filter.
 * @return envoy_dynamic_module_type_listener_filter_scheduler_module_ptr is the pointer to the
 * created listener filter scheduler.
 *
 * NOTE: it is caller's responsibility to delete the scheduler using
 * envoy_dynamic_module_callback_listener_filter_scheduler_delete when it is no longer needed.
 * See the comment on envoy_dynamic_module_type_listener_filter_scheduler_module_ptr.
 */
envoy_dynamic_module_type_listener_filter_scheduler_module_ptr
envoy_dynamic_module_callback_listener_filter_scheduler_new(
    envoy_dynamic_module_type_listener_filter_envoy_ptr filter_envoy_ptr);

/**
 * envoy_dynamic_module_callback_listener_filter_scheduler_commit is called by the module to
 * schedule a generic event to the listener filter on the worker thread it is running on.
 *
 * This will eventually end up invoking envoy_dynamic_module_on_listener_filter_scheduled
 * event hook on the worker thread.
 *
 * This can be called multiple times to schedule multiple events to the same filter.
 *
 * @param scheduler_module_ptr is the pointer to the listener filter scheduler created by
 * envoy_dynamic_module_callback_listener_filter_scheduler_new.
 * @param event_id is the ID of the event. This can be used to differentiate between multiple
 * events scheduled to the same filter. It can be any module-defined value.
 */
void envoy_dynamic_module_callback_listener_filter_scheduler_commit(
    envoy_dynamic_module_type_listener_filter_scheduler_module_ptr scheduler_module_ptr,
    uint64_t event_id);

/**
 * envoy_dynamic_module_callback_listener_filter_scheduler_delete is called by the module to delete
 * the listener filter scheduler created by
 * envoy_dynamic_module_callback_listener_filter_scheduler_new.
 *
 * @param scheduler_module_ptr is the pointer to the listener filter scheduler created by
 * envoy_dynamic_module_callback_listener_filter_scheduler_new.
 */
void envoy_dynamic_module_callback_listener_filter_scheduler_delete(
    envoy_dynamic_module_type_listener_filter_scheduler_module_ptr scheduler_module_ptr);

/**
 * envoy_dynamic_module_callback_listener_filter_config_scheduler_new is called by the module to
 * create a new listener filter configuration scheduler. The scheduler is used to dispatch listener
 * filter configuration operations to the main thread from any thread including the ones managed by
 * the module.
 *
 * @param filter_config_envoy_ptr is the pointer to the DynamicModuleListenerFilterConfig object.
 * @return envoy_dynamic_module_type_listener_filter_config_scheduler_module_ptr is the pointer to
 * the created listener filter configuration scheduler.
 *
 * NOTE: it is caller's responsibility to delete the scheduler using
 * envoy_dynamic_module_callback_listener_filter_config_scheduler_delete when it is no longer
 * needed. See the comment on envoy_dynamic_module_type_listener_filter_config_scheduler_module_ptr.
 */
envoy_dynamic_module_type_listener_filter_config_scheduler_module_ptr
envoy_dynamic_module_callback_listener_filter_config_scheduler_new(
    envoy_dynamic_module_type_listener_filter_config_envoy_ptr filter_config_envoy_ptr);

/**
 * envoy_dynamic_module_callback_listener_filter_config_scheduler_delete is called by the module to
 * delete the listener filter configuration scheduler created by
 * envoy_dynamic_module_callback_listener_filter_config_scheduler_new.
 *
 * @param scheduler_module_ptr is the pointer to the listener filter configuration scheduler
 * created by envoy_dynamic_module_callback_listener_filter_config_scheduler_new.
 */
void envoy_dynamic_module_callback_listener_filter_config_scheduler_delete(
    envoy_dynamic_module_type_listener_filter_config_scheduler_module_ptr scheduler_module_ptr);

/**
 * envoy_dynamic_module_callback_listener_filter_config_scheduler_commit is called by the module to
 * schedule a generic event to the listener filter configuration on the main thread.
 *
 * This will eventually end up invoking envoy_dynamic_module_on_listener_filter_config_scheduled
 * event hook on the main thread.
 *
 * This can be called multiple times to schedule multiple events to the same filter configuration.
 *
 * @param scheduler_module_ptr is the pointer to the listener filter configuration scheduler
 * created by envoy_dynamic_module_callback_listener_filter_config_scheduler_new.
 * @param event_id is the ID of the event. This can be used to differentiate between multiple
 * events scheduled to the same filter configuration. It can be any module-defined value.
 */
void envoy_dynamic_module_callback_listener_filter_config_scheduler_commit(
    envoy_dynamic_module_type_listener_filter_config_scheduler_module_ptr scheduler_module_ptr,
    uint64_t event_id);

// --------------------- Listener Filter Callbacks - Misc ---------------

/**
 * envoy_dynamic_module_callback_listener_filter_get_worker_index is called by the module to get the
 * worker index assigned to the current listener filter. This can be used by the module to manage
 * worker-specific resources or perform worker-specific logic.
 * @param filter_envoy_ptr is the pointer to the DynamicModuleListenerFilter object of the
 * corresponding listener filter.
 * @return the worker index assigned to the current listener filter.
 */
uint32_t envoy_dynamic_module_callback_listener_filter_get_worker_index(
    envoy_dynamic_module_type_listener_filter_envoy_ptr filter_envoy_ptr);

// =============================================================================
// ========================== UDP Listener Filter ==============================
// =============================================================================

// =============================================================================
// UDP Listener Filter Types
// =============================================================================

/**
 * envoy_dynamic_module_type_udp_listener_filter_config_envoy_ptr is a raw pointer to
 * the DynamicModuleUdpListenerFilterConfig class in Envoy.
 *
 * OWNERSHIP: Envoy owns the pointer.
 */
typedef void* envoy_dynamic_module_type_udp_listener_filter_config_envoy_ptr;

/**
 * envoy_dynamic_module_type_udp_listener_filter_config_module_ptr is a pointer to an in-module UDP
 * listener filter configuration.
 *
 * OWNERSHIP: The module is responsible for managing the lifetime of the pointer.
 */
typedef const void* envoy_dynamic_module_type_udp_listener_filter_config_module_ptr;

/**
 * envoy_dynamic_module_type_udp_listener_filter_envoy_ptr is a raw pointer to the
 * DynamicModuleUdpListenerFilter class in Envoy.
 *
 * OWNERSHIP: Envoy owns the pointer.
 */
typedef void* envoy_dynamic_module_type_udp_listener_filter_envoy_ptr;

/**
 * envoy_dynamic_module_type_udp_listener_filter_module_ptr is a pointer to an in-module UDP
 * listener filter.
 *
 * OWNERSHIP: The module is responsible for managing the lifetime of the pointer.
 */
typedef const void* envoy_dynamic_module_type_udp_listener_filter_module_ptr;

/**
 * envoy_dynamic_module_type_on_udp_listener_filter_status represents the status of the UDP
 * listener filter execution.
 */
typedef enum envoy_dynamic_module_type_on_udp_listener_filter_status {
  envoy_dynamic_module_type_on_udp_listener_filter_status_Continue,
  envoy_dynamic_module_type_on_udp_listener_filter_status_StopIteration,
} envoy_dynamic_module_type_on_udp_listener_filter_status;

// =============================================================================
// UDP Listener Filter Event Hooks
// =============================================================================

/**
 * envoy_dynamic_module_on_udp_listener_filter_config_new is called when a new UDP listener filter
 * configuration is created.
 */
envoy_dynamic_module_type_udp_listener_filter_config_module_ptr
envoy_dynamic_module_on_udp_listener_filter_config_new(
    envoy_dynamic_module_type_udp_listener_filter_config_envoy_ptr filter_config_envoy_ptr,
    envoy_dynamic_module_type_envoy_buffer name, envoy_dynamic_module_type_envoy_buffer config);

/**
 * envoy_dynamic_module_on_udp_listener_filter_config_destroy is called when the UDP listener filter
 * configuration is destroyed.
 */
void envoy_dynamic_module_on_udp_listener_filter_config_destroy(
    envoy_dynamic_module_type_udp_listener_filter_config_module_ptr filter_config_ptr);

/**
 * envoy_dynamic_module_on_udp_listener_filter_new is called when a new UDP listener filter is
 * created.
 */
envoy_dynamic_module_type_udp_listener_filter_module_ptr
envoy_dynamic_module_on_udp_listener_filter_new(
    envoy_dynamic_module_type_udp_listener_filter_config_module_ptr filter_config_ptr,
    envoy_dynamic_module_type_udp_listener_filter_envoy_ptr filter_envoy_ptr);

/**
 * envoy_dynamic_module_on_udp_listener_filter_on_data is called when a UDP packet is received.
 */
envoy_dynamic_module_type_on_udp_listener_filter_status
envoy_dynamic_module_on_udp_listener_filter_on_data(
    envoy_dynamic_module_type_udp_listener_filter_envoy_ptr filter_envoy_ptr,
    envoy_dynamic_module_type_udp_listener_filter_module_ptr filter_module_ptr);

/**
 * envoy_dynamic_module_on_udp_listener_filter_destroy is called when the UDP listener filter is
 * destroyed.
 */
void envoy_dynamic_module_on_udp_listener_filter_destroy(
    envoy_dynamic_module_type_udp_listener_filter_module_ptr filter_module_ptr);

// =============================================================================
// UDP Listener Filter Callbacks
// =============================================================================

/**
 * envoy_dynamic_module_callback_udp_listener_filter_get_datagram_data_chunks_size is called by the
 * module to get the number of chunks in the current datagram data. Combined with
 * envoy_dynamic_module_callback_udp_listener_filter_get_datagram_data_chunks, this can be used to
 * iterate over all chunks in the datagram. This is only valid during the
 * envoy_dynamic_module_on_udp_listener_filter_on_data callback.
 *
 * @param filter_envoy_ptr is the pointer to the DynamicModuleUdpListenerFilter object.
 * @return the number of chunks in the datagram data.
 */
size_t envoy_dynamic_module_callback_udp_listener_filter_get_datagram_data_chunks_size(
    envoy_dynamic_module_type_udp_listener_filter_envoy_ptr filter_envoy_ptr);

/**
 * envoy_dynamic_module_callback_udp_listener_filter_get_datagram_data_chunks is called by the
 * module to get the current datagram data as chunks. The module must ensure the provided buffer
 * array has enough capacity to store all chunks, which can be obtained via
 * envoy_dynamic_module_callback_udp_listener_filter_get_datagram_data_chunks_size. This is only
 * valid during the envoy_dynamic_module_on_udp_listener_filter_on_data callback.
 *
 * @param filter_envoy_ptr is the pointer to the DynamicModuleUdpListenerFilter object.
 * @param chunks_out is the output pointer to the array of buffer chunks owned by Envoy.
 * @return true if the datagram data is available and chunks_out is populated, false otherwise.
 */
bool envoy_dynamic_module_callback_udp_listener_filter_get_datagram_data_chunks(
    envoy_dynamic_module_type_udp_listener_filter_envoy_ptr filter_envoy_ptr,
    envoy_dynamic_module_type_envoy_buffer* chunks_out);

/**
 * envoy_dynamic_module_callback_udp_listener_filter_get_datagram_data_size is called by the module
 * to get the total length in bytes of the current datagram data. This is only valid during the
 * envoy_dynamic_module_on_udp_listener_filter_on_data callback.
 *
 * @param filter_envoy_ptr is the pointer to the DynamicModuleUdpListenerFilter object.
 * @return the total length in bytes of the datagram data.
 */
size_t envoy_dynamic_module_callback_udp_listener_filter_get_datagram_data_size(
    envoy_dynamic_module_type_udp_listener_filter_envoy_ptr filter_envoy_ptr);

/**
 * envoy_dynamic_module_callback_udp_listener_filter_set_datagram_data is called by the module to
 * set the current datagram data.
 */
bool envoy_dynamic_module_callback_udp_listener_filter_set_datagram_data(
    envoy_dynamic_module_type_udp_listener_filter_envoy_ptr filter_envoy_ptr,
    envoy_dynamic_module_type_module_buffer data);

/**
 * envoy_dynamic_module_callback_udp_listener_filter_get_peer_address is called by the module to
 * get the peer address.
 */
bool envoy_dynamic_module_callback_udp_listener_filter_get_peer_address(
    envoy_dynamic_module_type_udp_listener_filter_envoy_ptr filter_envoy_ptr,
    envoy_dynamic_module_type_envoy_buffer* address_out, uint32_t* port_out);

/**
 * envoy_dynamic_module_callback_udp_listener_filter_get_local_address is called by the module to
 * get the local address.
 */
bool envoy_dynamic_module_callback_udp_listener_filter_get_local_address(
    envoy_dynamic_module_type_udp_listener_filter_envoy_ptr filter_envoy_ptr,
    envoy_dynamic_module_type_envoy_buffer* address_out, uint32_t* port_out);

/**
 * envoy_dynamic_module_callback_udp_listener_filter_send_datagram is called by the module to
 * send a datagram.
 *
 * @return true if the datagram was sent, false otherwise.
 */
bool envoy_dynamic_module_callback_udp_listener_filter_send_datagram(
    envoy_dynamic_module_type_udp_listener_filter_envoy_ptr filter_envoy_ptr,
    envoy_dynamic_module_type_module_buffer data,
    envoy_dynamic_module_type_module_buffer peer_address, uint32_t peer_port);

// --------------------- UDP Listener Filter Callbacks - Metrics ---------------

/**
 * envoy_dynamic_module_callback_udp_listener_filter_config_define_counter is called by the module
 * during initialization to create a new Stats::Counter with the given name.
 *
 * @param config_envoy_ptr is the pointer to the DynamicModuleUdpListenerFilterConfig in which the
 * counter will be defined.
 * @param name is the name of the counter to be defined.
 * @param counter_id_ptr where the opaque ID that represents a unique metric will be stored. This
 * can be passed to envoy_dynamic_module_callback_udp_listener_filter_increment_counter together
 * with filter_envoy_ptr.
 * @return the result of the operation.
 */
envoy_dynamic_module_type_metrics_result
envoy_dynamic_module_callback_udp_listener_filter_config_define_counter(
    envoy_dynamic_module_type_udp_listener_filter_config_envoy_ptr config_envoy_ptr,
    envoy_dynamic_module_type_module_buffer name, size_t* counter_id_ptr);

/**
 * envoy_dynamic_module_callback_udp_listener_filter_increment_counter is called by the module to
 * increment a previously defined counter.
 *
 * @param filter_envoy_ptr is the pointer to the DynamicModuleUdpListenerFilter object.
 * @param id is the ID of the counter previously defined using the config.
 * @param value is the value to increment the counter by.
 * @return the result of the operation.
 */
envoy_dynamic_module_type_metrics_result
envoy_dynamic_module_callback_udp_listener_filter_increment_counter(
    envoy_dynamic_module_type_udp_listener_filter_envoy_ptr filter_envoy_ptr, size_t id,
    uint64_t value);

/**
 * envoy_dynamic_module_callback_udp_listener_filter_config_define_gauge is called by the module
 * during initialization to create a new Stats::Gauge with the given name.
 *
 * @param config_envoy_ptr is the pointer to the DynamicModuleUdpListenerFilterConfig in which the
 * gauge will be defined.
 * @param name is the name of the gauge to be defined.
 * @param gauge_id_ptr where the opaque ID that represents a unique metric will be stored.
 * @return the result of the operation.
 */
envoy_dynamic_module_type_metrics_result
envoy_dynamic_module_callback_udp_listener_filter_config_define_gauge(
    envoy_dynamic_module_type_udp_listener_filter_config_envoy_ptr config_envoy_ptr,
    envoy_dynamic_module_type_module_buffer name, size_t* gauge_id_ptr);

/**
 * envoy_dynamic_module_callback_udp_listener_filter_set_gauge is called by the module to set the
 * value of a previously defined gauge.
 *
 * @param filter_envoy_ptr is the pointer to the DynamicModuleUdpListenerFilter object.
 * @param id is the ID of the gauge previously defined using the config.
 * @param value is the value to set the gauge to.
 * @return the result of the operation.
 */
envoy_dynamic_module_type_metrics_result
envoy_dynamic_module_callback_udp_listener_filter_set_gauge(
    envoy_dynamic_module_type_udp_listener_filter_envoy_ptr filter_envoy_ptr, size_t id,
    uint64_t value);

/**
 * envoy_dynamic_module_callback_udp_listener_filter_increment_gauge is called by the module to
 * increase the value of a previously defined gauge.
 *
 * @param filter_envoy_ptr is the pointer to the DynamicModuleUdpListenerFilter object.
 * @param id is the ID of the gauge previously defined using the config.
 * @param value is the value to increase the gauge by.
 * @return the result of the operation.
 */
envoy_dynamic_module_type_metrics_result
envoy_dynamic_module_callback_udp_listener_filter_increment_gauge(
    envoy_dynamic_module_type_udp_listener_filter_envoy_ptr filter_envoy_ptr, size_t id,
    uint64_t value);

/**
 * envoy_dynamic_module_callback_udp_listener_filter_decrement_gauge is called by the module to
 * decrease the value of a previously defined gauge.
 *
 * @param filter_envoy_ptr is the pointer to the DynamicModuleUdpListenerFilter object.
 * @param id is the ID of the gauge previously defined using the config.
 * @param value is the value to decrease the gauge by.
 * @return the result of the operation.
 */
envoy_dynamic_module_type_metrics_result
envoy_dynamic_module_callback_udp_listener_filter_decrement_gauge(
    envoy_dynamic_module_type_udp_listener_filter_envoy_ptr filter_envoy_ptr, size_t id,
    uint64_t value);

/**
 * envoy_dynamic_module_callback_udp_listener_filter_config_define_histogram is called by the
 * module during initialization to create a new Stats::Histogram with the given name.
 *
 * @param config_envoy_ptr is the pointer to the DynamicModuleUdpListenerFilterConfig in which the
 * histogram will be defined.
 * @param name is the name of the histogram to be defined.
 * @param histogram_id_ptr where the opaque ID that represents a unique metric will be stored.
 * @return the result of the operation.
 */
envoy_dynamic_module_type_metrics_result
envoy_dynamic_module_callback_udp_listener_filter_config_define_histogram(
    envoy_dynamic_module_type_udp_listener_filter_config_envoy_ptr config_envoy_ptr,
    envoy_dynamic_module_type_module_buffer name, size_t* histogram_id_ptr);

/**
 * envoy_dynamic_module_callback_udp_listener_filter_record_histogram_value is called by the module
 * to record a value in a previously defined histogram.
 *
 * @param filter_envoy_ptr is the pointer to the DynamicModuleUdpListenerFilter object.
 * @param id is the ID of the histogram previously defined using the config.
 * @param value is the value to record in the histogram.
 * @return the result of the operation.
 */
envoy_dynamic_module_type_metrics_result
envoy_dynamic_module_callback_udp_listener_filter_record_histogram_value(
    envoy_dynamic_module_type_udp_listener_filter_envoy_ptr filter_envoy_ptr, size_t id,
    uint64_t value);

// --------------------- UDP Listener Filter Callbacks - Misc ---------------

/**
 * envoy_dynamic_module_callback_udp_listener_filter_get_worker_index is called by the module to get
 * the worker index assigned to the current UDP listener filter. This can be used by the module to
 * manage worker-specific resources or perform worker-specific logic.
 * @param filter_envoy_ptr is the pointer to the DynamicModuleUdpListenerFilter object of the
 * corresponding UDP listener filter.
 * @return the worker index assigned to the current UDP listener filter.
 */
uint32_t envoy_dynamic_module_callback_udp_listener_filter_get_worker_index(
    envoy_dynamic_module_type_udp_listener_filter_envoy_ptr filter_envoy_ptr);

// =============================================================================
// ============================== Access Logger ================================
// =============================================================================

// =============================================================================
// Access Logger Types
// =============================================================================

/**
 * envoy_dynamic_module_type_access_logger_config_envoy_ptr is a raw pointer to
 * the DynamicModuleAccessLogConfig class in Envoy.
 *
 * OWNERSHIP: Envoy owns the pointer.
 */
typedef void* envoy_dynamic_module_type_access_logger_config_envoy_ptr;

/**
 * envoy_dynamic_module_type_access_logger_config_module_ptr is a pointer to an in-module access
 * logger configuration.
 *
 * OWNERSHIP: The module is responsible for managing the lifetime of the pointer.
 */
typedef const void* envoy_dynamic_module_type_access_logger_config_module_ptr;

/**
 * envoy_dynamic_module_type_access_logger_envoy_ptr is a raw pointer to the
 * DynamicModuleAccessLog class in Envoy. This represents a single log event context.
 *
 * OWNERSHIP: Envoy owns the pointer. Valid only during the log event callback.
 */
typedef void* envoy_dynamic_module_type_access_logger_envoy_ptr;

/**
 * envoy_dynamic_module_type_access_logger_module_ptr is a pointer to an in-module access logger
 * instance. This can be per-thread or global depending on module implementation.
 *
 * OWNERSHIP: The module is responsible for managing the lifetime of the pointer.
 */
typedef const void* envoy_dynamic_module_type_access_logger_module_ptr;

/**
 * envoy_dynamic_module_type_access_log_type represents the type of access log event.
 * This corresponds to envoy::data::accesslog::v3::AccessLogType.
 */
typedef enum envoy_dynamic_module_type_access_log_type {
  envoy_dynamic_module_type_access_log_type_NotSet = 0,
  envoy_dynamic_module_type_access_log_type_TcpUpstreamConnected = 1,
  envoy_dynamic_module_type_access_log_type_TcpPeriodic = 2,
  envoy_dynamic_module_type_access_log_type_TcpConnectionEnd = 3,
  envoy_dynamic_module_type_access_log_type_DownstreamStart = 4,
  envoy_dynamic_module_type_access_log_type_DownstreamPeriodic = 5,
  envoy_dynamic_module_type_access_log_type_DownstreamEnd = 6,
  envoy_dynamic_module_type_access_log_type_UpstreamPoolReady = 7,
  envoy_dynamic_module_type_access_log_type_UpstreamPeriodic = 8,
  envoy_dynamic_module_type_access_log_type_UpstreamEnd = 9,
  envoy_dynamic_module_type_access_log_type_DownstreamTunnelSuccessfullyEstablished = 10,
  envoy_dynamic_module_type_access_log_type_UdpTunnelUpstreamConnected = 11,
  envoy_dynamic_module_type_access_log_type_UdpPeriodic = 12,
  envoy_dynamic_module_type_access_log_type_UdpSessionEnd = 13,
} envoy_dynamic_module_type_access_log_type;

/**
 * envoy_dynamic_module_type_response_flag represents a response flag from StreamInfo.
 * Values correspond to CoreResponseFlag enum.
 */
typedef enum envoy_dynamic_module_type_response_flag {
  envoy_dynamic_module_type_response_flag_FailedLocalHealthCheck = 0,
  envoy_dynamic_module_type_response_flag_NoHealthyUpstream = 1,
  envoy_dynamic_module_type_response_flag_UpstreamRequestTimeout = 2,
  envoy_dynamic_module_type_response_flag_LocalReset = 3,
  envoy_dynamic_module_type_response_flag_UpstreamRemoteReset = 4,
  envoy_dynamic_module_type_response_flag_UpstreamConnectionFailure = 5,
  envoy_dynamic_module_type_response_flag_UpstreamConnectionTermination = 6,
  envoy_dynamic_module_type_response_flag_UpstreamOverflow = 7,
  envoy_dynamic_module_type_response_flag_NoRouteFound = 8,
  envoy_dynamic_module_type_response_flag_DelayInjected = 9,
  envoy_dynamic_module_type_response_flag_FaultInjected = 10,
  envoy_dynamic_module_type_response_flag_RateLimited = 11,
  envoy_dynamic_module_type_response_flag_UnauthorizedExternalService = 12,
  envoy_dynamic_module_type_response_flag_RateLimitServiceError = 13,
  envoy_dynamic_module_type_response_flag_DownstreamConnectionTermination = 14,
  envoy_dynamic_module_type_response_flag_UpstreamRetryLimitExceeded = 15,
  envoy_dynamic_module_type_response_flag_StreamIdleTimeout = 16,
  envoy_dynamic_module_type_response_flag_InvalidEnvoyRequestHeaders = 17,
  envoy_dynamic_module_type_response_flag_DownstreamProtocolError = 18,
  envoy_dynamic_module_type_response_flag_UpstreamMaxStreamDurationReached = 19,
  envoy_dynamic_module_type_response_flag_ResponseFromCacheFilter = 20,
  envoy_dynamic_module_type_response_flag_NoFilterConfigFound = 21,
  envoy_dynamic_module_type_response_flag_DurationTimeout = 22,
  envoy_dynamic_module_type_response_flag_UpstreamProtocolError = 23,
  envoy_dynamic_module_type_response_flag_NoClusterFound = 24,
  envoy_dynamic_module_type_response_flag_OverloadManager = 25,
  envoy_dynamic_module_type_response_flag_DnsResolutionFailed = 26,
  envoy_dynamic_module_type_response_flag_DropOverLoad = 27,
  envoy_dynamic_module_type_response_flag_DownstreamRemoteReset = 28,
  envoy_dynamic_module_type_response_flag_UnconditionalDropOverload = 29,
} envoy_dynamic_module_type_response_flag;

/**
 * envoy_dynamic_module_type_timing_info contains timing information from StreamInfo.
 * All durations are in nanoseconds. A value of -1 indicates the timing is not available.
 */
typedef struct envoy_dynamic_module_type_timing_info {
  int64_t start_time_unix_ns;           // Request start time as Unix timestamp in nanoseconds.
  int64_t request_complete_duration_ns; // Duration from start to request complete.
  int64_t first_upstream_tx_byte_sent_ns;
  int64_t last_upstream_tx_byte_sent_ns;
  int64_t first_upstream_rx_byte_received_ns;
  int64_t last_upstream_rx_byte_received_ns;
  int64_t first_downstream_tx_byte_sent_ns;
  int64_t last_downstream_tx_byte_sent_ns;
} envoy_dynamic_module_type_timing_info;

/**
 * envoy_dynamic_module_type_bytes_info contains byte count information.
 */
typedef struct envoy_dynamic_module_type_bytes_info {
  uint64_t bytes_received;      // Total bytes received from downstream.
  uint64_t bytes_sent;          // Total bytes sent to downstream.
  uint64_t wire_bytes_received; // Wire bytes received (including TLS overhead).
  uint64_t wire_bytes_sent;     // Wire bytes sent (including TLS overhead).
} envoy_dynamic_module_type_bytes_info;

// =============================================================================
// Access Logger Event Hooks
// =============================================================================

/**
 * envoy_dynamic_module_on_access_logger_config_new is called when a new access logger
 * configuration is created. This is called on the main thread.
 *
 * @param config_envoy_ptr is the pointer to the DynamicModuleAccessLogConfig object.
 * @param name is the logger name.
 * @param config is the configuration for the logger.
 * @return a pointer to the in-module access logger configuration. Returning nullptr
 *         indicates a failure to initialize the module, and the configuration will be rejected.
 */
envoy_dynamic_module_type_access_logger_config_module_ptr
envoy_dynamic_module_on_access_logger_config_new(
    envoy_dynamic_module_type_access_logger_config_envoy_ptr config_envoy_ptr,
    envoy_dynamic_module_type_envoy_buffer name, envoy_dynamic_module_type_envoy_buffer config);

/**
 * envoy_dynamic_module_on_access_logger_config_destroy is called when the access logger
 * configuration is destroyed.
 *
 * @param config_module_ptr is a pointer to the in-module access logger configuration.
 */
void envoy_dynamic_module_on_access_logger_config_destroy(
    envoy_dynamic_module_type_access_logger_config_module_ptr config_module_ptr);

/**
 * envoy_dynamic_module_on_access_logger_new is called to create a new logger instance.
 * This may be called on any thread (typically per-worker thread for thread-local loggers).
 *
 * The module can choose to:
 * - Return a new instance per call (thread-local pattern, recommended for batching)
 * - Return the config_module_ptr itself if no per-instance state is needed
 * - Return a shared instance if the module handles thread safety internally
 *
 * @param config_module_ptr is the pointer to the in-module configuration.
 * @param logger_envoy_ptr is the pointer to the ThreadLocalLogger object. This can be used
 *        by the module to store a reference for later use in callbacks.
 * @return a pointer to the in-module logger instance. Returning nullptr will cause
 *         log events to be silently dropped.
 */
envoy_dynamic_module_type_access_logger_module_ptr envoy_dynamic_module_on_access_logger_new(
    envoy_dynamic_module_type_access_logger_config_module_ptr config_module_ptr,
    envoy_dynamic_module_type_access_logger_envoy_ptr logger_envoy_ptr);

/**
 * envoy_dynamic_module_on_access_logger_log is called when a log event occurs.
 * This is the main logging callback where the module should process the log entry.
 *
 * The logger_envoy_ptr is only valid during this callback. The module must not store
 * this pointer or use it after the callback returns.
 *
 * @param logger_envoy_ptr is the pointer to the Envoy log context (valid during this call only).
 * @param logger_module_ptr is the pointer to the in-module logger instance.
 * @param log_type is the type of access log event.
 */
void envoy_dynamic_module_on_access_logger_log(
    envoy_dynamic_module_type_access_logger_envoy_ptr logger_envoy_ptr,
    envoy_dynamic_module_type_access_logger_module_ptr logger_module_ptr,
    envoy_dynamic_module_type_access_log_type log_type);

/**
 * envoy_dynamic_module_on_access_logger_destroy is called when the logger instance
 * is destroyed.
 *
 * @param logger_module_ptr is the pointer to the in-module logger instance.
 */
void envoy_dynamic_module_on_access_logger_destroy(
    envoy_dynamic_module_type_access_logger_module_ptr logger_module_ptr);

/**
 * envoy_dynamic_module_on_access_logger_flush is called before the logger is destroyed
 * to give the module an opportunity to flush any buffered logs.
 *
 * This is called during shutdown when the ThreadLocalLogger is being destroyed.
 * Modules that buffer log entries should implement this to ensure no logs are lost.
 *
 * This is optional. If not implemented by the module, Envoy will skip calling it.
 *
 * @param logger_module_ptr is the pointer to the in-module logger instance.
 */
void envoy_dynamic_module_on_access_logger_flush(
    envoy_dynamic_module_type_access_logger_module_ptr logger_module_ptr);

// =============================================================================
// Access Logger Callbacks
// =============================================================================

// ---------------------- Access Logger Callbacks - Headers --------------------

/**
 * Get the number of headers in the specified header map.
 *
 * @param logger_envoy_ptr is the pointer to the log context.
 * @param header_type is the type of header map to access. Supported types are RequestHeader,
 *        ResponseHeader, and ResponseTrailer.
 * @return the number of headers, or 0 if the header map is not available.
 */
size_t envoy_dynamic_module_callback_access_logger_get_headers_size(
    envoy_dynamic_module_type_access_logger_envoy_ptr logger_envoy_ptr,
    envoy_dynamic_module_type_http_header_type header_type);

/**
 * Get all headers from the specified header map.
 *
 * @param logger_envoy_ptr is the pointer to the log context.
 * @param header_type is the type of header map to access. Supported types are RequestHeader,
 *        ResponseHeader, and ResponseTrailer.
 * @param result_headers is the output array (must be pre-allocated with correct size).
 * @return true if successful, false otherwise.
 */
bool envoy_dynamic_module_callback_access_logger_get_headers(
    envoy_dynamic_module_type_access_logger_envoy_ptr logger_envoy_ptr,
    envoy_dynamic_module_type_http_header_type header_type,
    envoy_dynamic_module_type_envoy_http_header* result_headers);

/**
 * Get a specific header value by key.
 *
 * @param logger_envoy_ptr is the pointer to the log context.
 * @param header_type is the type of header map to access. Supported types are RequestHeader,
 *        ResponseHeader, and ResponseTrailer.
 * @param key is the header key to look up.
 * @param result is the output buffer for the header value.
 * @param index is the index for multi-value headers (0 for first value).
 * @param total_count_out is optional output for total number of values with this key.
 * @return true if the header exists, false otherwise.
 */
bool envoy_dynamic_module_callback_access_logger_get_header_value(
    envoy_dynamic_module_type_access_logger_envoy_ptr logger_envoy_ptr,
    envoy_dynamic_module_type_http_header_type header_type,
    envoy_dynamic_module_type_module_buffer key, envoy_dynamic_module_type_envoy_buffer* result,
    size_t index, size_t* total_count_out);

// ------------------ Access Logger Callbacks - Stream Info Basic --------------

/**
 * @deprecated Use envoy_dynamic_module_callback_access_logger_get_attribute_int with
 * envoy_dynamic_module_type_attribute_id_ResponseCode instead.
 *
 * Get the HTTP response code.
 *
 * @param logger_envoy_ptr is the pointer to the log context.
 * @return the HTTP response code, or 0 if not available.
 */
uint32_t envoy_dynamic_module_callback_access_logger_get_response_code(
    envoy_dynamic_module_type_access_logger_envoy_ptr logger_envoy_ptr);

/**
 * @deprecated Use envoy_dynamic_module_callback_access_logger_get_attribute_string with
 * envoy_dynamic_module_type_attribute_id_ResponseCodeDetails instead.
 *
 * Get the response code details string.
 *
 * @param logger_envoy_ptr is the pointer to the log context.
 * @param result is the output buffer.
 * @return true if details are available, false otherwise.
 */
bool envoy_dynamic_module_callback_access_logger_get_response_code_details(
    envoy_dynamic_module_type_access_logger_envoy_ptr logger_envoy_ptr,
    envoy_dynamic_module_type_envoy_buffer* result);

/**
 * Check if a specific response flag is set.
 *
 * @param logger_envoy_ptr is the pointer to the log context.
 * @param flag is the response flag to check.
 * @return true if the flag is set, false otherwise.
 */
bool envoy_dynamic_module_callback_access_logger_has_response_flag(
    envoy_dynamic_module_type_access_logger_envoy_ptr logger_envoy_ptr,
    envoy_dynamic_module_type_response_flag flag);

/**
 * Get all response flags as a bitmask.
 *
 * @param logger_envoy_ptr is the pointer to the log context.
 * @return bitmask of response flags, or 0 if none are set.
 */
uint64_t envoy_dynamic_module_callback_access_logger_get_response_flags(
    envoy_dynamic_module_type_access_logger_envoy_ptr logger_envoy_ptr);

/**
 * @deprecated Use envoy_dynamic_module_callback_access_logger_get_attribute_string with
 * envoy_dynamic_module_type_attribute_id_RequestProtocol instead.
 *
 * Get the protocol (HTTP/1.0, HTTP/1.1, HTTP/2, HTTP/3).
 *
 * @param logger_envoy_ptr is the pointer to the log context.
 * @param result is the output buffer for the protocol string.
 * @return true if protocol is available, false otherwise.
 */
bool envoy_dynamic_module_callback_access_logger_get_protocol(
    envoy_dynamic_module_type_access_logger_envoy_ptr logger_envoy_ptr,
    envoy_dynamic_module_type_envoy_buffer* result);

/**
 * Get timing information from StreamInfo.
 *
 * This always populates the output struct. Individual fields are set to -1 if unavailable.
 *
 * @param logger_envoy_ptr is the pointer to the log context.
 * @param timing_out is the output parameter for timing info.
 */
void envoy_dynamic_module_callback_access_logger_get_timing_info(
    envoy_dynamic_module_type_access_logger_envoy_ptr logger_envoy_ptr,
    envoy_dynamic_module_type_timing_info* timing_out);

/**
 * Get byte count information from StreamInfo.
 *
 * This always populates the output struct. Individual fields are set to 0 if unavailable.
 *
 * @param logger_envoy_ptr is the pointer to the log context.
 * @param bytes_out is the output parameter for byte counts.
 */
void envoy_dynamic_module_callback_access_logger_get_bytes_info(
    envoy_dynamic_module_type_access_logger_envoy_ptr logger_envoy_ptr,
    envoy_dynamic_module_type_bytes_info* bytes_out);

/**
 * @deprecated Use envoy_dynamic_module_callback_access_logger_get_attribute_bool with
 * envoy_dynamic_module_type_attribute_id_HealthCheck instead.
 *
 * Check if this is a health check request.
 *
 * @param logger_envoy_ptr is the pointer to the log context.
 * @return true if this is a health check, false otherwise.
 */
bool envoy_dynamic_module_callback_access_logger_is_health_check(
    envoy_dynamic_module_type_access_logger_envoy_ptr logger_envoy_ptr);

/**
 * @deprecated Use envoy_dynamic_module_callback_access_logger_get_attribute_string with
 * envoy_dynamic_module_type_attribute_id_XdsRouteName instead.
 *
 * Get the route name.
 *
 * @param logger_envoy_ptr is the pointer to the log context.
 * @param result is the output buffer.
 * @return true if route name is available, false otherwise.
 */
bool envoy_dynamic_module_callback_access_logger_get_route_name(
    envoy_dynamic_module_type_access_logger_envoy_ptr logger_envoy_ptr,
    envoy_dynamic_module_type_envoy_buffer* result);

/**
 * @deprecated Use envoy_dynamic_module_callback_access_logger_get_attribute_string with
 * envoy_dynamic_module_type_attribute_id_XdsVirtualHostName instead.
 *
 * Get the virtual cluster name.
 *
 * @param logger_envoy_ptr is the pointer to the log context.
 * @param result is the output buffer.
 * @return true if virtual cluster name is available, false otherwise.
 */
bool envoy_dynamic_module_callback_access_logger_get_virtual_cluster_name(
    envoy_dynamic_module_type_access_logger_envoy_ptr logger_envoy_ptr,
    envoy_dynamic_module_type_envoy_buffer* result);

/**
 * @deprecated Use envoy_dynamic_module_callback_access_logger_get_attribute_int with
 * envoy_dynamic_module_type_attribute_id_UpstreamRequestAttemptCount instead.
 *
 * Get the upstream request attempt count.
 *
 * @param logger_envoy_ptr is the pointer to the log context.
 * @return the attempt count, or 0 if not available.
 */
uint32_t envoy_dynamic_module_callback_access_logger_get_attempt_count(
    envoy_dynamic_module_type_access_logger_envoy_ptr logger_envoy_ptr);

/**
 * @deprecated Use envoy_dynamic_module_callback_access_logger_get_attribute_string with
 * envoy_dynamic_module_type_attribute_id_ConnectionTerminationDetails instead.
 *
 * Get the connection termination details.
 *
 * @param logger_envoy_ptr is the pointer to the log context.
 * @param result is the output buffer.
 * @return true if termination details are available, false otherwise.
 */
bool envoy_dynamic_module_callback_access_logger_get_connection_termination_details(
    envoy_dynamic_module_type_access_logger_envoy_ptr logger_envoy_ptr,
    envoy_dynamic_module_type_envoy_buffer* result);

// -----------------Access Logger Callbacks - Address Information---------------

/**
 * Get the downstream remote address (client).
 *
 * @param logger_envoy_ptr is the pointer to the log context.
 * @param address_out is the output buffer for the IP address string.
 * @param port_out is the output parameter for the port.
 * @return true if address is available, false otherwise.
 */
bool envoy_dynamic_module_callback_access_logger_get_downstream_remote_address(
    envoy_dynamic_module_type_access_logger_envoy_ptr logger_envoy_ptr,
    envoy_dynamic_module_type_envoy_buffer* address_out, uint32_t* port_out);

/**
 * Get the downstream local address (Envoy listener).
 *
 * @param logger_envoy_ptr is the pointer to the log context.
 * @param address_out is the output buffer for the IP address string.
 * @param port_out is the output parameter for the port.
 * @return true if address is available, false otherwise.
 */
bool envoy_dynamic_module_callback_access_logger_get_downstream_local_address(
    envoy_dynamic_module_type_access_logger_envoy_ptr logger_envoy_ptr,
    envoy_dynamic_module_type_envoy_buffer* address_out, uint32_t* port_out);

/**
 * Get the downstream direct remote address (physical peer address before XFF processing).
 *
 * @param logger_envoy_ptr is the pointer to the log context.
 * @param address_out is the output buffer for the IP address string.
 * @param port_out is the output parameter for the port.
 * @return true if address is available, false otherwise.
 */
bool envoy_dynamic_module_callback_access_logger_get_downstream_direct_remote_address(
    envoy_dynamic_module_type_access_logger_envoy_ptr logger_envoy_ptr,
    envoy_dynamic_module_type_envoy_buffer* address_out, uint32_t* port_out);

/**
 * Get the downstream direct local address (physical listener address).
 *
 * @param logger_envoy_ptr is the pointer to the log context.
 * @param address_out is the output buffer for the IP address string.
 * @param port_out is the output parameter for the port.
 * @return true if address is available, false otherwise.
 */
bool envoy_dynamic_module_callback_access_logger_get_downstream_direct_local_address(
    envoy_dynamic_module_type_access_logger_envoy_ptr logger_envoy_ptr,
    envoy_dynamic_module_type_envoy_buffer* address_out, uint32_t* port_out);

/**
 * Get the upstream remote address (backend).
 *
 * @param logger_envoy_ptr is the pointer to the log context.
 * @param address_out is the output buffer for the IP address string.
 * @param port_out is the output parameter for the port.
 * @return true if address is available, false otherwise.
 */
bool envoy_dynamic_module_callback_access_logger_get_upstream_remote_address(
    envoy_dynamic_module_type_access_logger_envoy_ptr logger_envoy_ptr,
    envoy_dynamic_module_type_envoy_buffer* address_out, uint32_t* port_out);

/**
 * Get the upstream local address (Envoy outbound).
 *
 * @param logger_envoy_ptr is the pointer to the log context.
 * @param address_out is the output buffer for the IP address string.
 * @param port_out is the output parameter for the port.
 * @return true if address is available, false otherwise.
 */
bool envoy_dynamic_module_callback_access_logger_get_upstream_local_address(
    envoy_dynamic_module_type_access_logger_envoy_ptr logger_envoy_ptr,
    envoy_dynamic_module_type_envoy_buffer* address_out, uint32_t* port_out);

// ------------------- Access Logger Callbacks - Upstream Info -----------------

/**
 * Get the upstream cluster name.
 *
 * @param logger_envoy_ptr is the pointer to the log context.
 * @param result is the output buffer.
 * @return true if cluster name is available, false otherwise.
 */
bool envoy_dynamic_module_callback_access_logger_get_upstream_cluster(
    envoy_dynamic_module_type_access_logger_envoy_ptr logger_envoy_ptr,
    envoy_dynamic_module_type_envoy_buffer* result);

/**
 * Get the upstream host address (selected endpoint).
 *
 * @param logger_envoy_ptr is the pointer to the log context.
 * @param result is the output buffer.
 * @return true if host address is available, false otherwise.
 */
bool envoy_dynamic_module_callback_access_logger_get_upstream_host(
    envoy_dynamic_module_type_access_logger_envoy_ptr logger_envoy_ptr,
    envoy_dynamic_module_type_envoy_buffer* result);

/**
 * @deprecated Use envoy_dynamic_module_callback_access_logger_get_attribute_string with
 * envoy_dynamic_module_type_attribute_id_UpstreamTransportFailureReason instead.
 *
 * Get the upstream transport failure reason.
 *
 * @param logger_envoy_ptr is the pointer to the log context.
 * @param result is the output buffer.
 * @return true if failure reason is available, false otherwise.
 */
bool envoy_dynamic_module_callback_access_logger_get_upstream_transport_failure_reason(
    envoy_dynamic_module_type_access_logger_envoy_ptr logger_envoy_ptr,
    envoy_dynamic_module_type_envoy_buffer* result);

/**
 * Get the upstream connection ID.
 *
 * @param logger_envoy_ptr is the pointer to the log context.
 * @return the upstream connection ID, or 0 if not available.
 */
uint64_t envoy_dynamic_module_callback_access_logger_get_upstream_connection_id(
    envoy_dynamic_module_type_access_logger_envoy_ptr logger_envoy_ptr);

/**
 * Get the upstream TLS cipher suite. The buffer uses thread-local storage and is valid until the
 * next call to this function or `get_downstream_tls_cipher` on the same thread.
 *
 * @param logger_envoy_ptr is the pointer to the log context.
 * @param result is the output buffer.
 * @return true if cipher suite is available, false otherwise.
 */
bool envoy_dynamic_module_callback_access_logger_get_upstream_tls_cipher(
    envoy_dynamic_module_type_access_logger_envoy_ptr logger_envoy_ptr,
    envoy_dynamic_module_type_envoy_buffer* result);

/**
 * Get the upstream TLS session ID.
 *
 * @param logger_envoy_ptr is the pointer to the log context.
 * @param result is the output buffer.
 * @return true if session ID is available, false otherwise.
 */
bool envoy_dynamic_module_callback_access_logger_get_upstream_tls_session_id(
    envoy_dynamic_module_type_access_logger_envoy_ptr logger_envoy_ptr,
    envoy_dynamic_module_type_envoy_buffer* result);

/**
 * @deprecated Use envoy_dynamic_module_callback_access_logger_get_attribute_string with
 * envoy_dynamic_module_type_attribute_id_UpstreamTlsVersion instead.
 *
 * Get the upstream TLS version (e.g., "TLSv1.2", "TLSv1.3").
 *
 * @param logger_envoy_ptr is the pointer to the log context.
 * @param result is the output buffer.
 * @return true if TLS version is available, false otherwise.
 */
bool envoy_dynamic_module_callback_access_logger_get_upstream_tls_version(
    envoy_dynamic_module_type_access_logger_envoy_ptr logger_envoy_ptr,
    envoy_dynamic_module_type_envoy_buffer* result);

/**
 * @deprecated Use envoy_dynamic_module_callback_access_logger_get_attribute_string with
 * envoy_dynamic_module_type_attribute_id_UpstreamSubjectPeerCertificate instead.
 *
 * Get the upstream peer certificate subject.
 *
 * @param logger_envoy_ptr is the pointer to the log context.
 * @param result is the output buffer.
 * @return true if subject is available, false otherwise.
 */
bool envoy_dynamic_module_callback_access_logger_get_upstream_peer_subject(
    envoy_dynamic_module_type_access_logger_envoy_ptr logger_envoy_ptr,
    envoy_dynamic_module_type_envoy_buffer* result);

/**
 * Get the upstream peer certificate issuer.
 *
 * @param logger_envoy_ptr is the pointer to the log context.
 * @param result is the output buffer.
 * @return true if issuer is available, false otherwise.
 */
bool envoy_dynamic_module_callback_access_logger_get_upstream_peer_issuer(
    envoy_dynamic_module_type_access_logger_envoy_ptr logger_envoy_ptr,
    envoy_dynamic_module_type_envoy_buffer* result);

/**
 * @deprecated Use envoy_dynamic_module_callback_access_logger_get_attribute_string with
 * envoy_dynamic_module_type_attribute_id_UpstreamSubjectLocalCertificate instead.
 *
 * Get the upstream local certificate subject (Envoy's own certificate for the upstream connection).
 *
 * @param logger_envoy_ptr is the pointer to the log context.
 * @param result is the output buffer.
 * @return true if subject is available, false otherwise.
 */
bool envoy_dynamic_module_callback_access_logger_get_upstream_local_subject(
    envoy_dynamic_module_type_access_logger_envoy_ptr logger_envoy_ptr,
    envoy_dynamic_module_type_envoy_buffer* result);

/**
 * @deprecated Use envoy_dynamic_module_callback_access_logger_get_attribute_string with
 * envoy_dynamic_module_type_attribute_id_UpstreamSha256PeerCertificateDigest instead.
 *
 * Get the upstream peer certificate SHA256 fingerprint.
 *
 * @param logger_envoy_ptr is the pointer to the log context.
 * @param result is the output buffer.
 * @return true if fingerprint is available, false otherwise.
 */
bool envoy_dynamic_module_callback_access_logger_get_upstream_peer_cert_digest(
    envoy_dynamic_module_type_access_logger_envoy_ptr logger_envoy_ptr,
    envoy_dynamic_module_type_envoy_buffer* result);

/**
 * Get the upstream peer certificate validity start time.
 *
 * @param logger_envoy_ptr is the pointer to the log context.
 * @return epoch seconds of the certificate's notBefore field, or 0 if not available.
 */
int64_t envoy_dynamic_module_callback_access_logger_get_upstream_peer_cert_v_start(
    envoy_dynamic_module_type_access_logger_envoy_ptr logger_envoy_ptr);

/**
 * Get the upstream peer certificate validity end time.
 *
 * @param logger_envoy_ptr is the pointer to the log context.
 * @return epoch seconds of the certificate's notAfter field, or 0 if not available.
 */
int64_t envoy_dynamic_module_callback_access_logger_get_upstream_peer_cert_v_end(
    envoy_dynamic_module_type_access_logger_envoy_ptr logger_envoy_ptr);

/**
 * Get the count of URI Subject Alternative Names from the upstream peer certificate.
 *
 * @param logger_envoy_ptr is the pointer to the log context.
 * @return the count of URI SANs, or 0 if not available.
 */
size_t envoy_dynamic_module_callback_access_logger_get_upstream_peer_uri_san_size(
    envoy_dynamic_module_type_access_logger_envoy_ptr logger_envoy_ptr);

/**
 * Get the URI Subject Alternative Names from the upstream peer certificate. The module should
 * first call get_upstream_peer_uri_san_size to get the count and allocate the array.
 *
 * @param logger_envoy_ptr is the pointer to the log context.
 * @param sans_out is a pre-allocated array where Envoy will populate the SANs.
 * @return true if the SANs were populated successfully, false otherwise.
 */
bool envoy_dynamic_module_callback_access_logger_get_upstream_peer_uri_san(
    envoy_dynamic_module_type_access_logger_envoy_ptr logger_envoy_ptr,
    envoy_dynamic_module_type_envoy_buffer* sans_out);

/**
 * Get the count of URI Subject Alternative Names from the upstream local certificate.
 *
 * @param logger_envoy_ptr is the pointer to the log context.
 * @return the count of URI SANs, or 0 if not available.
 */
size_t envoy_dynamic_module_callback_access_logger_get_upstream_local_uri_san_size(
    envoy_dynamic_module_type_access_logger_envoy_ptr logger_envoy_ptr);

/**
 * Get the URI Subject Alternative Names from the upstream local certificate. The module should
 * first call get_upstream_local_uri_san_size to get the count and allocate the array.
 *
 * @param logger_envoy_ptr is the pointer to the log context.
 * @param sans_out is a pre-allocated array where Envoy will populate the SANs.
 * @return true if the SANs were populated successfully, false otherwise.
 */
bool envoy_dynamic_module_callback_access_logger_get_upstream_local_uri_san(
    envoy_dynamic_module_type_access_logger_envoy_ptr logger_envoy_ptr,
    envoy_dynamic_module_type_envoy_buffer* sans_out);

/**
 * Get the count of DNS Subject Alternative Names from the upstream peer certificate.
 *
 * @param logger_envoy_ptr is the pointer to the log context.
 * @return the count of DNS SANs, or 0 if not available.
 */
size_t envoy_dynamic_module_callback_access_logger_get_upstream_peer_dns_san_size(
    envoy_dynamic_module_type_access_logger_envoy_ptr logger_envoy_ptr);

/**
 * Get the DNS Subject Alternative Names from the upstream peer certificate. The module should
 * first call get_upstream_peer_dns_san_size to get the count and allocate the array.
 *
 * @param logger_envoy_ptr is the pointer to the log context.
 * @param sans_out is a pre-allocated array where Envoy will populate the SANs.
 * @return true if the SANs were populated successfully, false otherwise.
 */
bool envoy_dynamic_module_callback_access_logger_get_upstream_peer_dns_san(
    envoy_dynamic_module_type_access_logger_envoy_ptr logger_envoy_ptr,
    envoy_dynamic_module_type_envoy_buffer* sans_out);

/**
 * Get the count of DNS Subject Alternative Names from the upstream local certificate.
 *
 * @param logger_envoy_ptr is the pointer to the log context.
 * @return the count of DNS SANs, or 0 if not available.
 */
size_t envoy_dynamic_module_callback_access_logger_get_upstream_local_dns_san_size(
    envoy_dynamic_module_type_access_logger_envoy_ptr logger_envoy_ptr);

/**
 * Get the DNS Subject Alternative Names from the upstream local certificate. The module should
 * first call get_upstream_local_dns_san_size to get the count and allocate the array.
 *
 * @param logger_envoy_ptr is the pointer to the log context.
 * @param sans_out is a pre-allocated array where Envoy will populate the SANs.
 * @return true if the SANs were populated successfully, false otherwise.
 */
bool envoy_dynamic_module_callback_access_logger_get_upstream_local_dns_san(
    envoy_dynamic_module_type_access_logger_envoy_ptr logger_envoy_ptr,
    envoy_dynamic_module_type_envoy_buffer* sans_out);

// ------------------ Access Logger Callbacks - Connection/TLS Info ------------

/**
 * @deprecated Use envoy_dynamic_module_callback_access_logger_get_attribute_int with
 * envoy_dynamic_module_type_attribute_id_ConnectionId instead.
 *
 * Get the connection ID.
 *
 * @param logger_envoy_ptr is the pointer to the log context.
 * @return the connection ID, or 0 if not available.
 */
uint64_t envoy_dynamic_module_callback_access_logger_get_connection_id(
    envoy_dynamic_module_type_access_logger_envoy_ptr logger_envoy_ptr);

/**
 * @deprecated Use envoy_dynamic_module_callback_access_logger_get_attribute_bool with
 * envoy_dynamic_module_type_attribute_id_ConnectionMtls instead.
 *
 * Check if mTLS was used.
 *
 * @param logger_envoy_ptr is the pointer to the log context.
 * @return true if mTLS was used, false otherwise.
 */
bool envoy_dynamic_module_callback_access_logger_is_mtls(
    envoy_dynamic_module_type_access_logger_envoy_ptr logger_envoy_ptr);

/**
 * @deprecated Use envoy_dynamic_module_callback_access_logger_get_attribute_string with
 * envoy_dynamic_module_type_attribute_id_ConnectionRequestedServerName instead.
 *
 * Get the requested server name (SNI).
 *
 * @param logger_envoy_ptr is the pointer to the log context.
 * @param result is the output buffer.
 * @return true if SNI is available, false otherwise.
 */
bool envoy_dynamic_module_callback_access_logger_get_requested_server_name(
    envoy_dynamic_module_type_access_logger_envoy_ptr logger_envoy_ptr,
    envoy_dynamic_module_type_envoy_buffer* result);

/**
 * @deprecated Use envoy_dynamic_module_callback_access_logger_get_attribute_string with
 * envoy_dynamic_module_type_attribute_id_ConnectionTlsVersion instead.
 *
 * Get the downstream TLS version.
 *
 * @param logger_envoy_ptr is the pointer to the log context.
 * @param result is the output buffer.
 * @return true if TLS version is available, false otherwise.
 */
bool envoy_dynamic_module_callback_access_logger_get_downstream_tls_version(
    envoy_dynamic_module_type_access_logger_envoy_ptr logger_envoy_ptr,
    envoy_dynamic_module_type_envoy_buffer* result);

/**
 * @deprecated Use envoy_dynamic_module_callback_access_logger_get_attribute_string with
 * envoy_dynamic_module_type_attribute_id_ConnectionSubjectPeerCertificate instead.
 *
 * Get the downstream peer certificate subject.
 *
 * @param logger_envoy_ptr is the pointer to the log context.
 * @param result is the output buffer.
 * @return true if subject is available, false otherwise.
 */
bool envoy_dynamic_module_callback_access_logger_get_downstream_peer_subject(
    envoy_dynamic_module_type_access_logger_envoy_ptr logger_envoy_ptr,
    envoy_dynamic_module_type_envoy_buffer* result);

/**
 * @deprecated Use envoy_dynamic_module_callback_access_logger_get_attribute_string with
 * envoy_dynamic_module_type_attribute_id_ConnectionSha256PeerCertificateDigest instead.
 *
 * Get the downstream peer certificate SHA256 fingerprint.
 *
 * @param logger_envoy_ptr is the pointer to the log context.
 * @param result is the output buffer.
 * @return true if fingerprint is available, false otherwise.
 */
bool envoy_dynamic_module_callback_access_logger_get_downstream_peer_cert_digest(
    envoy_dynamic_module_type_access_logger_envoy_ptr logger_envoy_ptr,
    envoy_dynamic_module_type_envoy_buffer* result);

/**
 * Get the downstream TLS cipher suite. The buffer uses thread-local storage and is valid until the
 * next call to this function or `get_upstream_tls_cipher` on the same thread.
 *
 * @param logger_envoy_ptr is the pointer to the log context.
 * @param result is the output buffer.
 * @return true if cipher suite is available, false otherwise.
 */
bool envoy_dynamic_module_callback_access_logger_get_downstream_tls_cipher(
    envoy_dynamic_module_type_access_logger_envoy_ptr logger_envoy_ptr,
    envoy_dynamic_module_type_envoy_buffer* result);

/**
 * Get the downstream TLS session ID.
 *
 * @param logger_envoy_ptr is the pointer to the log context.
 * @param result is the output buffer.
 * @return true if session ID is available, false otherwise.
 */
bool envoy_dynamic_module_callback_access_logger_get_downstream_tls_session_id(
    envoy_dynamic_module_type_access_logger_envoy_ptr logger_envoy_ptr,
    envoy_dynamic_module_type_envoy_buffer* result);

/**
 * Get the downstream peer certificate issuer.
 *
 * @param logger_envoy_ptr is the pointer to the log context.
 * @param result is the output buffer.
 * @return true if issuer is available, false otherwise.
 */
bool envoy_dynamic_module_callback_access_logger_get_downstream_peer_issuer(
    envoy_dynamic_module_type_access_logger_envoy_ptr logger_envoy_ptr,
    envoy_dynamic_module_type_envoy_buffer* result);

/**
 * Get the downstream peer certificate serial number.
 *
 * @param logger_envoy_ptr is the pointer to the log context.
 * @param result is the output buffer.
 * @return true if serial number is available, false otherwise.
 */
bool envoy_dynamic_module_callback_access_logger_get_downstream_peer_serial(
    envoy_dynamic_module_type_access_logger_envoy_ptr logger_envoy_ptr,
    envoy_dynamic_module_type_envoy_buffer* result);

/**
 * Get the downstream peer certificate SHA1 fingerprint.
 *
 * @param logger_envoy_ptr is the pointer to the log context.
 * @param result is the output buffer.
 * @return true if fingerprint is available, false otherwise.
 */
bool envoy_dynamic_module_callback_access_logger_get_downstream_peer_fingerprint_1(
    envoy_dynamic_module_type_access_logger_envoy_ptr logger_envoy_ptr,
    envoy_dynamic_module_type_envoy_buffer* result);

/**
 * @deprecated Use envoy_dynamic_module_callback_access_logger_get_attribute_string with
 * envoy_dynamic_module_type_attribute_id_ConnectionSubjectLocalCertificate instead.
 *
 * Get the downstream local certificate subject (Envoy's own certificate).
 *
 * @param logger_envoy_ptr is the pointer to the log context.
 * @param result is the output buffer.
 * @return true if subject is available, false otherwise.
 */
bool envoy_dynamic_module_callback_access_logger_get_downstream_local_subject(
    envoy_dynamic_module_type_access_logger_envoy_ptr logger_envoy_ptr,
    envoy_dynamic_module_type_envoy_buffer* result);

/**
 * Check if the downstream peer certificate was presented.
 *
 * @param logger_envoy_ptr is the pointer to the log context.
 * @return true if a peer certificate was presented, false otherwise.
 */
bool envoy_dynamic_module_callback_access_logger_get_downstream_peer_cert_presented(
    envoy_dynamic_module_type_access_logger_envoy_ptr logger_envoy_ptr);

/**
 * Check if the downstream peer certificate was validated.
 *
 * @param logger_envoy_ptr is the pointer to the log context.
 * @return true if the peer certificate was validated, false otherwise.
 */
bool envoy_dynamic_module_callback_access_logger_get_downstream_peer_cert_validated(
    envoy_dynamic_module_type_access_logger_envoy_ptr logger_envoy_ptr);

/**
 * Get the downstream peer certificate validity start time.
 *
 * @param logger_envoy_ptr is the pointer to the log context.
 * @return epoch seconds of the certificate's notBefore field, or 0 if not available.
 */
int64_t envoy_dynamic_module_callback_access_logger_get_downstream_peer_cert_v_start(
    envoy_dynamic_module_type_access_logger_envoy_ptr logger_envoy_ptr);

/**
 * Get the downstream peer certificate validity end time.
 *
 * @param logger_envoy_ptr is the pointer to the log context.
 * @return epoch seconds of the certificate's notAfter field, or 0 if not available.
 */
int64_t envoy_dynamic_module_callback_access_logger_get_downstream_peer_cert_v_end(
    envoy_dynamic_module_type_access_logger_envoy_ptr logger_envoy_ptr);

/**
 * Get the count of URI Subject Alternative Names from the downstream peer certificate.
 *
 * @param logger_envoy_ptr is the pointer to the log context.
 * @return the count of URI SANs, or 0 if not available.
 */
size_t envoy_dynamic_module_callback_access_logger_get_downstream_peer_uri_san_size(
    envoy_dynamic_module_type_access_logger_envoy_ptr logger_envoy_ptr);

/**
 * Get the URI Subject Alternative Names from the downstream peer certificate. The module should
 * first call get_downstream_peer_uri_san_size to get the count and allocate the array.
 *
 * @param logger_envoy_ptr is the pointer to the log context.
 * @param sans_out is a pre-allocated array where Envoy will populate the SANs.
 * @return true if the SANs were populated successfully, false otherwise.
 */
bool envoy_dynamic_module_callback_access_logger_get_downstream_peer_uri_san(
    envoy_dynamic_module_type_access_logger_envoy_ptr logger_envoy_ptr,
    envoy_dynamic_module_type_envoy_buffer* sans_out);

/**
 * Get the count of URI Subject Alternative Names from the downstream local certificate.
 *
 * @param logger_envoy_ptr is the pointer to the log context.
 * @return the count of URI SANs, or 0 if not available.
 */
size_t envoy_dynamic_module_callback_access_logger_get_downstream_local_uri_san_size(
    envoy_dynamic_module_type_access_logger_envoy_ptr logger_envoy_ptr);

/**
 * Get the URI Subject Alternative Names from the downstream local certificate. The module should
 * first call get_downstream_local_uri_san_size to get the count and allocate the array.
 *
 * @param logger_envoy_ptr is the pointer to the log context.
 * @param sans_out is a pre-allocated array where Envoy will populate the SANs.
 * @return true if the SANs were populated successfully, false otherwise.
 */
bool envoy_dynamic_module_callback_access_logger_get_downstream_local_uri_san(
    envoy_dynamic_module_type_access_logger_envoy_ptr logger_envoy_ptr,
    envoy_dynamic_module_type_envoy_buffer* sans_out);

/**
 * Get the count of DNS Subject Alternative Names from the downstream peer certificate.
 *
 * @param logger_envoy_ptr is the pointer to the log context.
 * @return the count of DNS SANs, or 0 if not available.
 */
size_t envoy_dynamic_module_callback_access_logger_get_downstream_peer_dns_san_size(
    envoy_dynamic_module_type_access_logger_envoy_ptr logger_envoy_ptr);

/**
 * Get the DNS Subject Alternative Names from the downstream peer certificate. The module should
 * first call get_downstream_peer_dns_san_size to get the count and allocate the array.
 *
 * @param logger_envoy_ptr is the pointer to the log context.
 * @param sans_out is a pre-allocated array where Envoy will populate the SANs.
 * @return true if the SANs were populated successfully, false otherwise.
 */
bool envoy_dynamic_module_callback_access_logger_get_downstream_peer_dns_san(
    envoy_dynamic_module_type_access_logger_envoy_ptr logger_envoy_ptr,
    envoy_dynamic_module_type_envoy_buffer* sans_out);

/**
 * Get the count of DNS Subject Alternative Names from the downstream local certificate.
 *
 * @param logger_envoy_ptr is the pointer to the log context.
 * @return the count of DNS SANs, or 0 if not available.
 */
size_t envoy_dynamic_module_callback_access_logger_get_downstream_local_dns_san_size(
    envoy_dynamic_module_type_access_logger_envoy_ptr logger_envoy_ptr);

/**
 * Get the DNS Subject Alternative Names from the downstream local certificate. The module should
 * first call get_downstream_local_dns_san_size to get the count and allocate the array.
 *
 * @param logger_envoy_ptr is the pointer to the log context.
 * @param sans_out is a pre-allocated array where Envoy will populate the SANs.
 * @return true if the SANs were populated successfully, false otherwise.
 */
bool envoy_dynamic_module_callback_access_logger_get_downstream_local_dns_san(
    envoy_dynamic_module_type_access_logger_envoy_ptr logger_envoy_ptr,
    envoy_dynamic_module_type_envoy_buffer* sans_out);

// ---------------- Access Logger Callbacks - Metadata and Dynamic State -------

/**
 * Get a value from dynamic metadata by filter name and key path.
 *
 * @param logger_envoy_ptr is the pointer to the log context.
 * @param filter_name is the filter namespace in dynamic metadata.
 * @param path is the key path within the filter namespace (can be nested with dots).
 * @param result is the output buffer (JSON encoded for complex values).
 * @return true if value exists, false otherwise.
 */
bool envoy_dynamic_module_callback_access_logger_get_dynamic_metadata(
    envoy_dynamic_module_type_access_logger_envoy_ptr logger_envoy_ptr,
    envoy_dynamic_module_type_module_buffer filter_name,
    envoy_dynamic_module_type_module_buffer path, envoy_dynamic_module_type_envoy_buffer* result);

/**
 * Get a value from filter state by key.
 *
 * @param logger_envoy_ptr is the pointer to the log context.
 * @param key is the filter state key.
 * @param result is the output buffer (serialized representation).
 * @return true if value exists, false otherwise.
 */
bool envoy_dynamic_module_callback_access_logger_get_filter_state(
    envoy_dynamic_module_type_access_logger_envoy_ptr logger_envoy_ptr,
    envoy_dynamic_module_type_module_buffer key, envoy_dynamic_module_type_envoy_buffer* result);

/**
 * @deprecated Use envoy_dynamic_module_callback_access_logger_get_attribute_string with
 * envoy_dynamic_module_type_attribute_id_RequestId instead.
 *
 * Get the request ID (x-request-id header value or generated).
 *
 * @param logger_envoy_ptr is the pointer to the log context.
 * @param result is the output buffer.
 * @return true if request ID is available, false otherwise.
 */
bool envoy_dynamic_module_callback_access_logger_get_request_id(
    envoy_dynamic_module_type_access_logger_envoy_ptr logger_envoy_ptr,
    envoy_dynamic_module_type_envoy_buffer* result);

/**
 * Get the local reply body (if this was a local response).
 *
 * @param logger_envoy_ptr is the pointer to the log context.
 * @param result is the output buffer.
 * @return true if local reply body exists, false otherwise.
 */
bool envoy_dynamic_module_callback_access_logger_get_local_reply_body(
    envoy_dynamic_module_type_access_logger_envoy_ptr logger_envoy_ptr,
    envoy_dynamic_module_type_envoy_buffer* result);

// ------------------- Access Logger Callbacks - Tracing -----------------------

/**
 * Get the trace ID from the active span.
 *
 * @param logger_envoy_ptr is the pointer to the log context.
 * @param result is the output buffer.
 * @return true if trace ID is available, false otherwise.
 */
bool envoy_dynamic_module_callback_access_logger_get_trace_id(
    envoy_dynamic_module_type_access_logger_envoy_ptr logger_envoy_ptr,
    envoy_dynamic_module_type_envoy_buffer* result);

/**
 * Get the span ID from the active span.
 *
 * @param logger_envoy_ptr is the pointer to the log context.
 * @param result is the output buffer.
 * @return true if span ID is available, false otherwise.
 */
bool envoy_dynamic_module_callback_access_logger_get_span_id(
    envoy_dynamic_module_type_access_logger_envoy_ptr logger_envoy_ptr,
    envoy_dynamic_module_type_envoy_buffer* result);

/**
 * Check if the request was sampled for tracing.
 *
 * @param logger_envoy_ptr is the pointer to the log context.
 * @return true if sampled, false otherwise.
 */
bool envoy_dynamic_module_callback_access_logger_is_trace_sampled(
    envoy_dynamic_module_type_access_logger_envoy_ptr logger_envoy_ptr);

// -----------------------------------------------------------------------------
// Access Logger Callbacks - Additional Stream Info
// -----------------------------------------------------------------------------

/**
 * Get the `JA3` fingerprint hash from the downstream connection.
 *
 * @param logger_envoy_ptr is the pointer to the log context.
 * @param result is the output buffer where the `JA3` hash string owned by Envoy will be stored.
 * @return true if the `JA3` hash is available, false otherwise.
 */
bool envoy_dynamic_module_callback_access_logger_get_ja3_hash(
    envoy_dynamic_module_type_access_logger_envoy_ptr logger_envoy_ptr,
    envoy_dynamic_module_type_envoy_buffer* result);

/**
 * Get the `JA4` fingerprint hash from the downstream connection.
 *
 * @param logger_envoy_ptr is the pointer to the log context.
 * @param result is the output buffer where the `JA4` hash string owned by Envoy will be stored.
 * @return true if the `JA4` hash is available, false otherwise.
 */
bool envoy_dynamic_module_callback_access_logger_get_ja4_hash(
    envoy_dynamic_module_type_access_logger_envoy_ptr logger_envoy_ptr,
    envoy_dynamic_module_type_envoy_buffer* result);

/**
 * @deprecated Use envoy_dynamic_module_callback_access_logger_get_attribute_string with
 * envoy_dynamic_module_type_attribute_id_ConnectionTransportFailureReason instead.
 *
 * Get the downstream transport failure reason.
 *
 * @param logger_envoy_ptr is the pointer to the log context.
 * @param result is the output buffer where the failure reason string will be stored.
 * @return true if the failure reason is available, false otherwise.
 */
bool envoy_dynamic_module_callback_access_logger_get_downstream_transport_failure_reason(
    envoy_dynamic_module_type_access_logger_envoy_ptr logger_envoy_ptr,
    envoy_dynamic_module_type_envoy_buffer* result);

/**
 * Get the byte size of request headers (uncompressed).
 *
 * @param logger_envoy_ptr is the pointer to the log context.
 * @return the byte size of request headers, or 0 if not available.
 */
uint64_t envoy_dynamic_module_callback_access_logger_get_request_headers_bytes(
    envoy_dynamic_module_type_access_logger_envoy_ptr logger_envoy_ptr);

/**
 * Get the byte size of response headers (uncompressed).
 *
 * @param logger_envoy_ptr is the pointer to the log context.
 * @return the byte size of response headers, or 0 if not available.
 */
uint64_t envoy_dynamic_module_callback_access_logger_get_response_headers_bytes(
    envoy_dynamic_module_type_access_logger_envoy_ptr logger_envoy_ptr);

/**
 * Get the byte size of response trailers (uncompressed).
 *
 * @param logger_envoy_ptr is the pointer to the log context.
 * @return the byte size of response trailers, or 0 if not available.
 */
uint64_t envoy_dynamic_module_callback_access_logger_get_response_trailers_bytes(
    envoy_dynamic_module_type_access_logger_envoy_ptr logger_envoy_ptr);

/**
 * Get the upstream protocol (e.g., "HTTP/1.1", "HTTP/2").
 *
 * @param logger_envoy_ptr is the pointer to the log context.
 * @param result is the output buffer where the protocol string will be stored.
 * @return true if the upstream protocol is available, false otherwise.
 */
bool envoy_dynamic_module_callback_access_logger_get_upstream_protocol(
    envoy_dynamic_module_type_access_logger_envoy_ptr logger_envoy_ptr,
    envoy_dynamic_module_type_envoy_buffer* result);

/**
 * Get the upstream connection pool ready duration in nanoseconds.
 * This is the time from when the upstream request was created to when the connection pool
 * became ready.
 *
 * @param logger_envoy_ptr is the pointer to the log context.
 * @return the duration in nanoseconds, or -1 if not available.
 */
int64_t envoy_dynamic_module_callback_access_logger_get_upstream_pool_ready_duration_ns(
    envoy_dynamic_module_type_access_logger_envoy_ptr logger_envoy_ptr);

// -----------------------------------------------------------------------------
// Access Logger Callbacks - Generic Attribute Accessors
// -----------------------------------------------------------------------------
// These callbacks provide a generic attribute-based interface for accessing
// stream info data, following the same pattern as the HTTP filter attribute
// accessors. They use the same envoy_dynamic_module_type_attribute_id enum.

/**
 * envoy_dynamic_module_callback_access_logger_get_attribute_string is called by the module to get
 * a string attribute value from the access log context. If the attribute is not accessible or the
 * value is not a string, this returns false.
 *
 * @param logger_envoy_ptr is the pointer to the log context.
 * @param attribute_id is the ID of the attribute.
 * @param result is the pointer to the buffer where the string value will be stored.
 * @return true if the operation is successful, false otherwise.
 */
bool envoy_dynamic_module_callback_access_logger_get_attribute_string(
    envoy_dynamic_module_type_access_logger_envoy_ptr logger_envoy_ptr,
    envoy_dynamic_module_type_attribute_id attribute_id,
    envoy_dynamic_module_type_envoy_buffer* result);

/**
 * envoy_dynamic_module_callback_access_logger_get_attribute_int is called by the module to get
 * an integer attribute value from the access log context. If the attribute is not accessible or the
 * value is not an integer, this returns false.
 *
 * @param logger_envoy_ptr is the pointer to the log context.
 * @param attribute_id is the ID of the attribute.
 * @param result is the pointer to the variable where the integer value will be stored.
 * @return true if the operation is successful, false otherwise.
 */
bool envoy_dynamic_module_callback_access_logger_get_attribute_int(
    envoy_dynamic_module_type_access_logger_envoy_ptr logger_envoy_ptr,
    envoy_dynamic_module_type_attribute_id attribute_id, uint64_t* result);

/**
 * envoy_dynamic_module_callback_access_logger_get_attribute_bool is called by the module to get
 * a boolean attribute value from the access log context. If the attribute is not accessible or the
 * value is not a boolean, this returns false.
 *
 * @param logger_envoy_ptr is the pointer to the log context.
 * @param attribute_id is the ID of the attribute.
 * @param result is the pointer to the variable where the boolean value will be stored.
 * @return true if the operation is successful, false otherwise.
 */
bool envoy_dynamic_module_callback_access_logger_get_attribute_bool(
    envoy_dynamic_module_type_access_logger_envoy_ptr logger_envoy_ptr,
    envoy_dynamic_module_type_attribute_id attribute_id, bool* result);

// -----------------------------------------------------------------------------
// Access Logger Callbacks - Metrics
// -----------------------------------------------------------------------------

/**
 * envoy_dynamic_module_callback_access_logger_config_define_counter is called by the module during
 * initialization to create a new Stats::Counter with the given name.
 *
 * @param config_envoy_ptr is the pointer to the DynamicModuleAccessLogConfig in which the
 * counter will be defined.
 * @param name is the name of the counter to be defined.
 * @param counter_id_ptr where the opaque ID that represents a unique metric will be stored. This
 * can be passed to envoy_dynamic_module_callback_access_logger_increment_counter together with
 * config_envoy_ptr.
 * @return the result of the operation.
 */
envoy_dynamic_module_type_metrics_result
envoy_dynamic_module_callback_access_logger_config_define_counter(
    envoy_dynamic_module_type_access_logger_config_envoy_ptr config_envoy_ptr,
    envoy_dynamic_module_type_module_buffer name, size_t* counter_id_ptr);

/**
 * envoy_dynamic_module_callback_access_logger_increment_counter is called by the module to
 * increment a previously defined counter.
 *
 * @param config_envoy_ptr is the pointer to the DynamicModuleAccessLogConfig.
 * @param id is the ID of the counter previously defined using this config.
 * @param value is the value to increment the counter by.
 * @return the result of the operation.
 */
envoy_dynamic_module_type_metrics_result
envoy_dynamic_module_callback_access_logger_increment_counter(
    envoy_dynamic_module_type_access_logger_config_envoy_ptr config_envoy_ptr, size_t id,
    uint64_t value);

/**
 * envoy_dynamic_module_callback_access_logger_config_define_gauge is called by the module during
 * initialization to create a new Stats::Gauge with the given name.
 *
 * @param config_envoy_ptr is the pointer to the DynamicModuleAccessLogConfig in which the
 * gauge will be defined.
 * @param name is the name of the gauge to be defined.
 * @param gauge_id_ptr where the opaque ID that represents a unique metric will be stored.
 * @return the result of the operation.
 */
envoy_dynamic_module_type_metrics_result
envoy_dynamic_module_callback_access_logger_config_define_gauge(
    envoy_dynamic_module_type_access_logger_config_envoy_ptr config_envoy_ptr,
    envoy_dynamic_module_type_module_buffer name, size_t* gauge_id_ptr);

/**
 * envoy_dynamic_module_callback_access_logger_set_gauge is called by the module to set the value
 * of a previously defined gauge.
 *
 * @param config_envoy_ptr is the pointer to the DynamicModuleAccessLogConfig.
 * @param id is the ID of the gauge previously defined using this config.
 * @param value is the value to set the gauge to.
 * @return the result of the operation.
 */
envoy_dynamic_module_type_metrics_result envoy_dynamic_module_callback_access_logger_set_gauge(
    envoy_dynamic_module_type_access_logger_config_envoy_ptr config_envoy_ptr, size_t id,
    uint64_t value);

/**
 * envoy_dynamic_module_callback_access_logger_increment_gauge is called by the module to increase
 * the value of a previously defined gauge.
 *
 * @param config_envoy_ptr is the pointer to the DynamicModuleAccessLogConfig.
 * @param id is the ID of the gauge previously defined using this config.
 * @param value is the value to increase the gauge by.
 * @return the result of the operation.
 */
envoy_dynamic_module_type_metrics_result
envoy_dynamic_module_callback_access_logger_increment_gauge(
    envoy_dynamic_module_type_access_logger_config_envoy_ptr config_envoy_ptr, size_t id,
    uint64_t value);

/**
 * envoy_dynamic_module_callback_access_logger_decrement_gauge is called by the module to decrease
 * the value of a previously defined gauge.
 *
 * @param config_envoy_ptr is the pointer to the DynamicModuleAccessLogConfig.
 * @param id is the ID of the gauge previously defined using this config.
 * @param value is the value to decrease the gauge by.
 * @return the result of the operation.
 */
envoy_dynamic_module_type_metrics_result
envoy_dynamic_module_callback_access_logger_decrement_gauge(
    envoy_dynamic_module_type_access_logger_config_envoy_ptr config_envoy_ptr, size_t id,
    uint64_t value);

/**
 * envoy_dynamic_module_callback_access_logger_config_define_histogram is called by the module
 * during initialization to create a new Stats::Histogram with the given name.
 *
 * @param config_envoy_ptr is the pointer to the DynamicModuleAccessLogConfig in which the
 * histogram will be defined.
 * @param name is the name of the histogram to be defined.
 * @param histogram_id_ptr where the opaque ID that represents a unique metric will be stored.
 * @return the result of the operation.
 */
envoy_dynamic_module_type_metrics_result
envoy_dynamic_module_callback_access_logger_config_define_histogram(
    envoy_dynamic_module_type_access_logger_config_envoy_ptr config_envoy_ptr,
    envoy_dynamic_module_type_module_buffer name, size_t* histogram_id_ptr);

/**
 * envoy_dynamic_module_callback_access_logger_record_histogram_value is called by the module to
 * record a value in a previously defined histogram.
 *
 * @param config_envoy_ptr is the pointer to the DynamicModuleAccessLogConfig.
 * @param id is the ID of the histogram previously defined using this config.
 * @param value is the value to record in the histogram.
 * @return the result of the operation.
 */
envoy_dynamic_module_type_metrics_result
envoy_dynamic_module_callback_access_logger_record_histogram_value(
    envoy_dynamic_module_type_access_logger_config_envoy_ptr config_envoy_ptr, size_t id,
    uint64_t value);

// --------------------- Access Logger Callbacks - Misc ---------------

/**
 * envoy_dynamic_module_callback_access_logger_get_worker_index is called by the module to get the
 * worker index assigned to the current access logger. This can be used by the module to manage
 * worker-specific resources or perform worker-specific logic.
 * @param access_logger_envoy_ptr is the pointer to the DynamicModuleAccessLogger object of the
 * corresponding access logger.
 * @return the worker index assigned to the current access logger. For the main thread the index
 * will be equal to envoy_dynamic_module_callback_get_concurrency result.
 */
uint32_t envoy_dynamic_module_callback_access_logger_get_worker_index(
    envoy_dynamic_module_type_access_logger_envoy_ptr access_logger_envoy_ptr);

// =============================================================================
// =========================== Bootstrap Extension =============================
// =============================================================================

// =============================================================================
// Bootstrap Extension Types
// =============================================================================

/**
 * envoy_dynamic_module_type_bootstrap_extension_config_envoy_ptr is a raw pointer to the
 * DynamicModuleBootstrapExtensionConfig class in Envoy.
 *
 * OWNERSHIP: Envoy owns the pointer.
 */
typedef void* envoy_dynamic_module_type_bootstrap_extension_config_envoy_ptr;

/**
 * envoy_dynamic_module_type_bootstrap_extension_config_module_ptr is a pointer to an in-module
 * bootstrap extension configuration.
 *
 * OWNERSHIP: The module is responsible for managing the lifetime of the pointer.
 */
typedef const void* envoy_dynamic_module_type_bootstrap_extension_config_module_ptr;

/**
 * envoy_dynamic_module_type_bootstrap_extension_envoy_ptr is a raw pointer to the
 * DynamicModuleBootstrapExtension class in Envoy.
 *
 * OWNERSHIP: Envoy owns the pointer.
 */
typedef void* envoy_dynamic_module_type_bootstrap_extension_envoy_ptr;

/**
 * envoy_dynamic_module_type_bootstrap_extension_module_ptr is a pointer to an in-module bootstrap
 * extension.
 *
 * OWNERSHIP: The module is responsible for managing the lifetime of the pointer.
 */
typedef const void* envoy_dynamic_module_type_bootstrap_extension_module_ptr;

/**
 * envoy_dynamic_module_type_bootstrap_extension_config_scheduler_module_ptr is a raw pointer to
 * the DynamicModuleBootstrapExtensionConfigScheduler class in Envoy.
 *
 * OWNERSHIP: The allocation is done by Envoy but the module is responsible for managing the
 * lifetime of the pointer. Notably, it must be explicitly destroyed by the module
 * when scheduling the bootstrap extension config event is done. The creation of this pointer is
 * done by envoy_dynamic_module_callback_bootstrap_extension_config_scheduler_new and the scheduling
 * and destruction is done by
 * envoy_dynamic_module_callback_bootstrap_extension_config_scheduler_delete. Since its lifecycle is
 * owned/managed by the module, this has _module_ptr suffix.
 */
typedef void* envoy_dynamic_module_type_bootstrap_extension_config_scheduler_module_ptr;

/**
 * envoy_dynamic_module_type_bootstrap_extension_timer_module_ptr is a raw pointer to the
 * DynamicModuleBootstrapExtensionTimer class in Envoy.
 *
 * OWNERSHIP: The allocation is done by Envoy but the module is responsible for managing the
 * lifetime of the pointer. Notably, it must be explicitly destroyed by the module
 * when the timer is no longer needed. The creation of this pointer is done by
 * envoy_dynamic_module_callback_bootstrap_extension_timer_new and the destruction is done by
 * envoy_dynamic_module_callback_bootstrap_extension_timer_delete. Since its lifecycle is
 * owned/managed by the module, this has _module_ptr suffix.
 */
typedef void* envoy_dynamic_module_type_bootstrap_extension_timer_module_ptr;

/**
 * File watcher event constants. These correspond to Envoy's Filesystem::Watcher::Events.
 * These are bitmask values that can be OR'd together.
 */
typedef enum envoy_dynamic_module_type_file_watcher_event {
  envoy_dynamic_module_type_file_watcher_event_MovedTo = 0x1,
  envoy_dynamic_module_type_file_watcher_event_Modified = 0x2,
} envoy_dynamic_module_type_file_watcher_event;

// =============================================================================
// Bootstrap Extension Event Hooks
// =============================================================================

/**
 * envoy_dynamic_module_on_bootstrap_extension_config_new is called by the main thread when the
 * bootstrap extension config is loaded. The function returns a
 * envoy_dynamic_module_type_bootstrap_extension_config_module_ptr for the given name and config.
 *
 * @param extension_config_envoy_ptr is the pointer to the DynamicModuleBootstrapExtensionConfig
 * object for the corresponding config.
 * @param name is the name of the extension.
 * @param config is the configuration for the extension.
 * @return envoy_dynamic_module_type_bootstrap_extension_config_module_ptr is the pointer to the
 * in-module bootstrap extension configuration. Returning nullptr indicates a failure to initialize
 * the module. When it fails, the extension configuration will be rejected.
 */
envoy_dynamic_module_type_bootstrap_extension_config_module_ptr
envoy_dynamic_module_on_bootstrap_extension_config_new(
    envoy_dynamic_module_type_bootstrap_extension_config_envoy_ptr extension_config_envoy_ptr,
    envoy_dynamic_module_type_envoy_buffer name, envoy_dynamic_module_type_envoy_buffer config);

/**
 * envoy_dynamic_module_on_bootstrap_extension_config_destroy is called when the bootstrap extension
 * configuration is destroyed in Envoy. The module should release any resources associated with the
 * corresponding in-module bootstrap extension configuration.
 *
 * @param extension_config_ptr is the pointer to the in-module bootstrap extension configuration
 * whose corresponding Envoy bootstrap extension configuration is being destroyed.
 */
void envoy_dynamic_module_on_bootstrap_extension_config_destroy(
    envoy_dynamic_module_type_bootstrap_extension_config_module_ptr extension_config_ptr);

/**
 * envoy_dynamic_module_on_bootstrap_extension_new is called when a new bootstrap extension is
 * created.
 *
 * @param extension_config_ptr is the pointer to the in-module bootstrap extension configuration.
 * @param extension_envoy_ptr is the pointer to the DynamicModuleBootstrapExtension object of the
 * corresponding bootstrap extension.
 * @return envoy_dynamic_module_type_bootstrap_extension_module_ptr is the pointer to the in-module
 * bootstrap extension. Returning nullptr indicates a failure to initialize the module. When it
 * fails, the extension will be rejected.
 */
envoy_dynamic_module_type_bootstrap_extension_module_ptr
envoy_dynamic_module_on_bootstrap_extension_new(
    envoy_dynamic_module_type_bootstrap_extension_config_module_ptr extension_config_ptr,
    envoy_dynamic_module_type_bootstrap_extension_envoy_ptr extension_envoy_ptr);

/**
 * envoy_dynamic_module_on_bootstrap_extension_server_initialized is called when the server is
 * initialized. This is called on the main thread after the ServerFactoryContext is fully
 * initialized.
 *
 * @param extension_envoy_ptr is the pointer to the DynamicModuleBootstrapExtension object.
 * @param extension_module_ptr is the pointer to the in-module bootstrap extension.
 */
void envoy_dynamic_module_on_bootstrap_extension_server_initialized(
    envoy_dynamic_module_type_bootstrap_extension_envoy_ptr extension_envoy_ptr,
    envoy_dynamic_module_type_bootstrap_extension_module_ptr extension_module_ptr);

/**
 * envoy_dynamic_module_on_bootstrap_extension_worker_thread_initialized is called when a worker
 * thread is initialized. This is called once per worker thread.
 *
 * @param extension_envoy_ptr is the pointer to the DynamicModuleBootstrapExtension object.
 * @param extension_module_ptr is the pointer to the in-module bootstrap extension.
 */
void envoy_dynamic_module_on_bootstrap_extension_worker_thread_initialized(
    envoy_dynamic_module_type_bootstrap_extension_envoy_ptr extension_envoy_ptr,
    envoy_dynamic_module_type_bootstrap_extension_module_ptr extension_module_ptr);

/**
 * envoy_dynamic_module_on_bootstrap_extension_drain_started is called when Envoy begins draining.
 *
 * This is called on the main thread before workers are stopped. The module can still make HTTP
 * callouts and use timers during drain. This is the appropriate place to close persistent
 * connections, stop background tasks, or de-register from service discovery.
 *
 * @param extension_envoy_ptr is the pointer to the DynamicModuleBootstrapExtension object.
 * @param extension_module_ptr is the pointer to the in-module bootstrap extension.
 */
void envoy_dynamic_module_on_bootstrap_extension_drain_started(
    envoy_dynamic_module_type_bootstrap_extension_envoy_ptr extension_envoy_ptr,
    envoy_dynamic_module_type_bootstrap_extension_module_ptr extension_module_ptr);

/**
 * envoy_dynamic_module_type_event_cb is a function pointer type used for completion callbacks.
 *
 * When Envoy passes a completion callback to a module, the module MUST invoke it exactly once
 * when it has finished its asynchronous work. Envoy will wait for the callback before proceeding.
 *
 * @param context is the opaque context pointer that was passed alongside this callback. The module
 * must pass it back unchanged when invoking the callback.
 */
typedef void (*envoy_dynamic_module_type_event_cb)(void* context);

/**
 * envoy_dynamic_module_on_bootstrap_extension_shutdown is called when Envoy is about to exit.
 *
 * This is called on the main thread during the ShutdownExit lifecycle stage. The module MUST
 * invoke the completion callback exactly once with the provided context when it has finished
 * cleanup. Envoy will wait for the callback before terminating. This is the appropriate place to
 * flush batched data, close connections, or signal external systems that this Envoy instance is
 * going away.
 *
 * @param extension_envoy_ptr is the pointer to the DynamicModuleBootstrapExtension object.
 * @param extension_module_ptr is the pointer to the in-module bootstrap extension.
 * @param completion_callback is the callback that must be invoked when shutdown cleanup is done.
 * @param completion_context is the opaque context pointer to pass to the completion callback.
 */
void envoy_dynamic_module_on_bootstrap_extension_shutdown(
    envoy_dynamic_module_type_bootstrap_extension_envoy_ptr extension_envoy_ptr,
    envoy_dynamic_module_type_bootstrap_extension_module_ptr extension_module_ptr,
    envoy_dynamic_module_type_event_cb completion_callback, void* completion_context);

/**
 * envoy_dynamic_module_on_bootstrap_extension_destroy is called when the bootstrap extension is
 * destroyed.
 *
 * @param extension_module_ptr is the pointer to the in-module bootstrap extension.
 */
void envoy_dynamic_module_on_bootstrap_extension_destroy(
    envoy_dynamic_module_type_bootstrap_extension_module_ptr extension_module_ptr);

/**
 * envoy_dynamic_module_on_bootstrap_extension_config_scheduled is called when the bootstrap
 * extension configuration is scheduled to be executed on the main thread with
 * envoy_dynamic_module_callback_bootstrap_extension_config_scheduler_commit callback.
 *
 * @param extension_config_envoy_ptr is the pointer to the DynamicModuleBootstrapExtensionConfig
 * object.
 * @param extension_config_ptr is the pointer to the in-module bootstrap extension configuration
 * created by envoy_dynamic_module_on_bootstrap_extension_config_new.
 * @param event_id is the ID of the event passed to
 * envoy_dynamic_module_callback_bootstrap_extension_config_scheduler_commit.
 */
void envoy_dynamic_module_on_bootstrap_extension_config_scheduled(
    envoy_dynamic_module_type_bootstrap_extension_config_envoy_ptr extension_config_envoy_ptr,
    envoy_dynamic_module_type_bootstrap_extension_config_module_ptr extension_config_ptr,
    uint64_t event_id);

/**
 * envoy_dynamic_module_on_bootstrap_extension_timer_fired is called when a timer created by
 * envoy_dynamic_module_callback_bootstrap_extension_timer_new fires on the main thread.
 *
 * @param extension_config_envoy_ptr is the pointer to the DynamicModuleBootstrapExtensionConfig
 * object.
 * @param extension_config_module_ptr is the pointer to the in-module bootstrap extension
 * configuration created by envoy_dynamic_module_on_bootstrap_extension_config_new.
 * @param timer_ptr is the pointer to the timer that fired. The module can re-enable this timer
 * by calling envoy_dynamic_module_callback_bootstrap_extension_timer_enable again.
 */
void envoy_dynamic_module_on_bootstrap_extension_timer_fired(
    envoy_dynamic_module_type_bootstrap_extension_config_envoy_ptr extension_config_envoy_ptr,
    envoy_dynamic_module_type_bootstrap_extension_config_module_ptr extension_config_module_ptr,
    envoy_dynamic_module_type_bootstrap_extension_timer_module_ptr timer_ptr);

/**
 * envoy_dynamic_module_on_bootstrap_extension_file_changed is called when a file watched by
 * envoy_dynamic_module_callback_bootstrap_extension_file_watcher_add_watch changes on the main
 * thread.
 *
 * @param extension_config_envoy_ptr is the pointer to the DynamicModuleBootstrapExtensionConfig
 * object.
 * @param extension_config_module_ptr is the pointer to the in-module bootstrap extension
 * configuration created by envoy_dynamic_module_on_bootstrap_extension_config_new.
 * @param path is the path that was registered via
 * envoy_dynamic_module_callback_bootstrap_extension_file_watcher_add_watch that triggered the
 * change. This is owned by the callback closure and valid only for the duration of this call.
 * @param events is the bitmask of events that occurred (MovedTo = 0x1, Modified = 0x2).
 */
void envoy_dynamic_module_on_bootstrap_extension_file_changed(
    envoy_dynamic_module_type_bootstrap_extension_config_envoy_ptr extension_config_envoy_ptr,
    envoy_dynamic_module_type_bootstrap_extension_config_module_ptr extension_config_module_ptr,
    envoy_dynamic_module_type_envoy_buffer path, uint32_t events);

/**
 * envoy_dynamic_module_on_bootstrap_extension_cluster_add_or_update is called when a cluster is
 * added to or updated in the ClusterManager.
 *
 * This is only called if the module has opted in to receiving cluster lifecycle events via
 * envoy_dynamic_module_callback_bootstrap_extension_enable_cluster_lifecycle. The callback is
 * registered on the main thread and invoked on the main thread.
 *
 * @param extension_config_envoy_ptr is the pointer to the DynamicModuleBootstrapExtensionConfig
 * object.
 * @param extension_config_module_ptr is the pointer to the in-module bootstrap extension
 * configuration created by envoy_dynamic_module_on_bootstrap_extension_config_new.
 * @param cluster_name is the name of the cluster that was added or updated.
 */
void envoy_dynamic_module_on_bootstrap_extension_cluster_add_or_update(
    envoy_dynamic_module_type_bootstrap_extension_config_envoy_ptr extension_config_envoy_ptr,
    envoy_dynamic_module_type_bootstrap_extension_config_module_ptr extension_config_module_ptr,
    envoy_dynamic_module_type_envoy_buffer cluster_name);

/**
 * envoy_dynamic_module_on_bootstrap_extension_cluster_removal is called when a cluster is
 * removed from the ClusterManager.
 *
 * This is only called if the module has opted in to receiving cluster lifecycle events via
 * envoy_dynamic_module_callback_bootstrap_extension_enable_cluster_lifecycle. The callback is
 * registered on the main thread and invoked on the main thread.
 *
 * @param extension_config_envoy_ptr is the pointer to the DynamicModuleBootstrapExtensionConfig
 * object.
 * @param extension_config_module_ptr is the pointer to the in-module bootstrap extension
 * configuration created by envoy_dynamic_module_on_bootstrap_extension_config_new.
 * @param cluster_name is the name of the cluster that was removed.
 */
void envoy_dynamic_module_on_bootstrap_extension_cluster_removal(
    envoy_dynamic_module_type_bootstrap_extension_config_envoy_ptr extension_config_envoy_ptr,
    envoy_dynamic_module_type_bootstrap_extension_config_module_ptr extension_config_module_ptr,
    envoy_dynamic_module_type_envoy_buffer cluster_name);

/**
 * envoy_dynamic_module_on_bootstrap_extension_listener_add_or_update is called when a listener is
 * added to or updated in the ListenerManager.
 *
 * This is only called if the module has opted in to receiving listener lifecycle events via
 * envoy_dynamic_module_callback_bootstrap_extension_enable_listener_lifecycle. The callback is
 * registered on the main thread and invoked on the main thread.
 *
 * @param extension_config_envoy_ptr is the pointer to the DynamicModuleBootstrapExtensionConfig
 * object.
 * @param extension_config_module_ptr is the pointer to the in-module bootstrap extension
 * configuration created by envoy_dynamic_module_on_bootstrap_extension_config_new.
 * @param listener_name is the name of the listener that was added or updated.
 */
void envoy_dynamic_module_on_bootstrap_extension_listener_add_or_update(
    envoy_dynamic_module_type_bootstrap_extension_config_envoy_ptr extension_config_envoy_ptr,
    envoy_dynamic_module_type_bootstrap_extension_config_module_ptr extension_config_module_ptr,
    envoy_dynamic_module_type_envoy_buffer listener_name);

/**
 * envoy_dynamic_module_on_bootstrap_extension_listener_removal is called when a listener is
 * removed from the ListenerManager.
 *
 * This is only called if the module has opted in to receiving listener lifecycle events via
 * envoy_dynamic_module_callback_bootstrap_extension_enable_listener_lifecycle. The callback is
 * registered on the main thread and invoked on the main thread.
 *
 * @param extension_config_envoy_ptr is the pointer to the DynamicModuleBootstrapExtensionConfig
 * object.
 * @param extension_config_module_ptr is the pointer to the in-module bootstrap extension
 * configuration created by envoy_dynamic_module_on_bootstrap_extension_config_new.
 * @param listener_name is the name of the listener that was removed.
 */
void envoy_dynamic_module_on_bootstrap_extension_listener_removal(
    envoy_dynamic_module_type_bootstrap_extension_config_envoy_ptr extension_config_envoy_ptr,
    envoy_dynamic_module_type_bootstrap_extension_config_module_ptr extension_config_module_ptr,
    envoy_dynamic_module_type_envoy_buffer listener_name);

// =============================================================================
// Bootstrap Extension Callbacks
// =============================================================================

/**
 * envoy_dynamic_module_callback_bootstrap_extension_config_scheduler_new is called by the module
 * to create a new bootstrap extension configuration scheduler. The scheduler is used to dispatch
 * bootstrap extension configuration operations to the main thread from any thread including the
 * ones managed by the module.
 *
 * @param extension_config_envoy_ptr is the pointer to the DynamicModuleBootstrapExtensionConfig
 * object.
 * @return envoy_dynamic_module_type_bootstrap_extension_config_scheduler_module_ptr is the pointer
 * to the created bootstrap extension configuration scheduler.
 *
 * NOTE: it is caller's responsibility to delete the scheduler using
 * envoy_dynamic_module_callback_bootstrap_extension_config_scheduler_delete when it is no longer
 * needed. See the comment on
 * envoy_dynamic_module_type_bootstrap_extension_config_scheduler_module_ptr.
 */
envoy_dynamic_module_type_bootstrap_extension_config_scheduler_module_ptr
envoy_dynamic_module_callback_bootstrap_extension_config_scheduler_new(
    envoy_dynamic_module_type_bootstrap_extension_config_envoy_ptr extension_config_envoy_ptr);

/**
 * envoy_dynamic_module_callback_bootstrap_extension_config_scheduler_delete is called by the
 * module to delete the bootstrap extension configuration scheduler created by
 * envoy_dynamic_module_callback_bootstrap_extension_config_scheduler_new.
 *
 * @param scheduler_module_ptr is the pointer to the bootstrap extension configuration scheduler
 * created by envoy_dynamic_module_callback_bootstrap_extension_config_scheduler_new.
 */
void envoy_dynamic_module_callback_bootstrap_extension_config_scheduler_delete(
    envoy_dynamic_module_type_bootstrap_extension_config_scheduler_module_ptr scheduler_module_ptr);

/**
 * envoy_dynamic_module_callback_bootstrap_extension_config_scheduler_commit is called by the
 * module to schedule a generic event to the bootstrap extension configuration on the main thread.
 *
 * This will eventually end up invoking envoy_dynamic_module_on_bootstrap_extension_config_scheduled
 * event hook on the main thread.
 *
 * This can be called multiple times to schedule multiple events to the same configuration.
 *
 * @param scheduler_module_ptr is the pointer to the bootstrap extension configuration scheduler
 * created by envoy_dynamic_module_callback_bootstrap_extension_config_scheduler_new.
 * @param event_id is the ID of the event. This can be used to differentiate between multiple
 * events scheduled to the same configuration. It can be any module-defined value.
 */
void envoy_dynamic_module_callback_bootstrap_extension_config_scheduler_commit(
    envoy_dynamic_module_type_bootstrap_extension_config_scheduler_module_ptr scheduler_module_ptr,
    uint64_t event_id);

// -----------------------------------------------------------------------------
// Bootstrap Extension - HTTP Client
// -----------------------------------------------------------------------------

/**
 * envoy_dynamic_module_on_bootstrap_extension_http_callout_done is called when the HTTP callout
 * response is received initiated by a bootstrap extension.
 *
 * @param extension_config_envoy_ptr is the pointer to the DynamicModuleBootstrapExtensionConfig
 * object.
 * @param extension_config_module_ptr is the pointer to the in-module bootstrap extension
 * configuration created by envoy_dynamic_module_on_bootstrap_extension_config_new.
 * @param callout_id is the ID of the callout. This is used to differentiate between multiple
 * calls.
 * @param result is the result of the callout.
 * @param headers is the headers of the response.
 * @param headers_size is the size of the headers.
 * @param body_chunks is the body of the response.
 * @param body_chunks_size is the size of the body.
 *
 * headers and body_chunks are owned by Envoy, and they are guaranteed to be valid until the end of
 * this event hook. They may be null if the callout fails or the response is empty.
 */
void envoy_dynamic_module_on_bootstrap_extension_http_callout_done(
    envoy_dynamic_module_type_bootstrap_extension_config_envoy_ptr extension_config_envoy_ptr,
    envoy_dynamic_module_type_bootstrap_extension_config_module_ptr extension_config_module_ptr,
    uint64_t callout_id, envoy_dynamic_module_type_http_callout_result result,
    envoy_dynamic_module_type_envoy_http_header* headers, size_t headers_size,
    envoy_dynamic_module_type_envoy_buffer* body_chunks, size_t body_chunks_size);

/**
 * envoy_dynamic_module_callback_bootstrap_extension_http_callout is called by the module to
 * initiate an HTTP callout. The callout is initiated by the bootstrap extension and the response
 * is received in envoy_dynamic_module_on_bootstrap_extension_http_callout_done.
 *
 * This must be called on the main thread. To call from other threads, use the scheduler mechanism
 * to post an event to the main thread first.
 *
 * @param extension_config_envoy_ptr is the pointer to the DynamicModuleBootstrapExtensionConfig
 * object.
 * @param callout_id_out is a pointer to a variable where the callout ID will be stored. This can be
 * arbitrary and is used to differentiate between multiple calls from the same extension.
 * @param cluster_name is the name of the cluster to which the callout is sent.
 * @param headers is the headers of the request. It must contain :method, :path and host headers.
 * @param headers_size is the size of the headers.
 * @param body is the body of the request.
 * @param timeout_milliseconds is the timeout for the callout in milliseconds.
 * @return envoy_dynamic_module_type_http_callout_init_result is the result of the callout.
 */
envoy_dynamic_module_type_http_callout_init_result
envoy_dynamic_module_callback_bootstrap_extension_http_callout(
    envoy_dynamic_module_type_bootstrap_extension_config_envoy_ptr extension_config_envoy_ptr,
    uint64_t* callout_id_out, envoy_dynamic_module_type_module_buffer cluster_name,
    envoy_dynamic_module_type_module_http_header* headers, size_t headers_size,
    envoy_dynamic_module_type_module_buffer body, uint64_t timeout_milliseconds);

// -----------------------------------------------------------------------------
// Bootstrap Extension - Init Manager Integration
// -----------------------------------------------------------------------------

/**
 * envoy_dynamic_module_callback_bootstrap_extension_config_signal_init_complete is called by the
 * module to signal that the bootstrap extension has completed its initialization. Envoy
 * automatically registers an init target for every bootstrap extension, blocking traffic until
 * the module signals readiness by calling this function.
 *
 * The module must call this exactly once during or after
 * envoy_dynamic_module_on_bootstrap_extension_config_new to unblock Envoy. If the module does not
 * require asynchronous initialization, it should call this immediately during config creation.
 *
 * This must be called on the main thread. To call from other threads, use the scheduler mechanism
 * to post an event to the main thread first.
 *
 * @param extension_config_envoy_ptr is the pointer to the DynamicModuleBootstrapExtensionConfig
 * object.
 */
void envoy_dynamic_module_callback_bootstrap_extension_config_signal_init_complete(
    envoy_dynamic_module_type_bootstrap_extension_config_envoy_ptr extension_config_envoy_ptr);

// -------------------- Bootstrap Extension Callbacks - Stats Access --------------------

/**
 * envoy_dynamic_module_type_stats_iteration_action represents the action to take after each
 * stat is visited during iteration.
 */
typedef enum {
  // Continue iterating.
  envoy_dynamic_module_type_stats_iteration_action_Continue = 0,
  // Stop iterating.
  envoy_dynamic_module_type_stats_iteration_action_Stop = 1,
} envoy_dynamic_module_type_stats_iteration_action;

/**
 * envoy_dynamic_module_callback_bootstrap_extension_get_counter_value is called by the module to
 * get the current value of a counter by name.
 *
 * @param extension_envoy_ptr is the pointer to the DynamicModuleBootstrapExtension object.
 * @param name is the name of the counter to find.
 * @param value_ptr is where the value will be stored if the counter is found.
 * @return true if the counter was found and value_ptr was populated, false otherwise.
 */
bool envoy_dynamic_module_callback_bootstrap_extension_get_counter_value(
    envoy_dynamic_module_type_bootstrap_extension_envoy_ptr extension_envoy_ptr,
    envoy_dynamic_module_type_module_buffer name, uint64_t* value_ptr);

/**
 * envoy_dynamic_module_callback_bootstrap_extension_get_gauge_value is called by the module to
 * get the current value of a gauge by name.
 *
 * @param extension_envoy_ptr is the pointer to the DynamicModuleBootstrapExtension object.
 * @param name is the name of the gauge to find.
 * @param value_ptr is where the value will be stored if the gauge is found.
 * @return true if the gauge was found and value_ptr was populated, false otherwise.
 */
bool envoy_dynamic_module_callback_bootstrap_extension_get_gauge_value(
    envoy_dynamic_module_type_bootstrap_extension_envoy_ptr extension_envoy_ptr,
    envoy_dynamic_module_type_module_buffer name, uint64_t* value_ptr);

/**
 * envoy_dynamic_module_callback_bootstrap_extension_get_histogram_summary is called by the module
 * to get the summary statistics of a histogram by name. This returns the cumulative statistics
 * since the server started.
 *
 * @param extension_envoy_ptr is the pointer to the DynamicModuleBootstrapExtension object.
 * @param name is the name of the histogram to find.
 * @param sample_count_ptr is where the sample count will be stored if the histogram is found.
 * @param sample_sum_ptr is where the sample sum will be stored if the histogram is found.
 * @return true if the histogram was found and the output pointers were populated, false otherwise.
 */
bool envoy_dynamic_module_callback_bootstrap_extension_get_histogram_summary(
    envoy_dynamic_module_type_bootstrap_extension_envoy_ptr extension_envoy_ptr,
    envoy_dynamic_module_type_module_buffer name, uint64_t* sample_count_ptr,
    double* sample_sum_ptr);

/**
 * The callback type for iterating counters.
 *
 * @param name is the name of the counter.
 * @param value is the current value of the counter.
 * @param user_data is the user data passed to the iterate function.
 * @return the action to take after visiting this counter.
 */
typedef envoy_dynamic_module_type_stats_iteration_action (
    *envoy_dynamic_module_type_counter_iterator_fn)(envoy_dynamic_module_type_envoy_buffer name,
                                                    uint64_t value, void* user_data);

/**
 * envoy_dynamic_module_callback_bootstrap_extension_iterate_counters is called by the module to
 * iterate over all counters in the stats store.
 *
 * @param extension_envoy_ptr is the pointer to the DynamicModuleBootstrapExtension object.
 * @param iterator_fn is the callback function to call for each counter.
 * @param user_data is the user data to pass to the callback function.
 */
void envoy_dynamic_module_callback_bootstrap_extension_iterate_counters(
    envoy_dynamic_module_type_bootstrap_extension_envoy_ptr extension_envoy_ptr,
    envoy_dynamic_module_type_counter_iterator_fn iterator_fn, void* user_data);

/**
 * The callback type for iterating gauges.
 *
 * @param name is the name of the gauge.
 * @param value is the current value of the gauge.
 * @param user_data is the user data passed to the iterate function.
 * @return the action to take after visiting this gauge.
 */
typedef envoy_dynamic_module_type_stats_iteration_action (
    *envoy_dynamic_module_type_gauge_iterator_fn)(envoy_dynamic_module_type_envoy_buffer name,
                                                  uint64_t value, void* user_data);

/**
 * envoy_dynamic_module_callback_bootstrap_extension_iterate_gauges is called by the module to
 * iterate over all gauges in the stats store.
 *
 * @param extension_envoy_ptr is the pointer to the DynamicModuleBootstrapExtension object.
 * @param iterator_fn is the callback function to call for each gauge.
 * @param user_data is the user data to pass to the callback function.
 */
void envoy_dynamic_module_callback_bootstrap_extension_iterate_gauges(
    envoy_dynamic_module_type_bootstrap_extension_envoy_ptr extension_envoy_ptr,
    envoy_dynamic_module_type_gauge_iterator_fn iterator_fn, void* user_data);

// -------------------- Bootstrap Extension Callbacks - Metrics Define/Update --------------------

/**
 * envoy_dynamic_module_callback_bootstrap_extension_config_define_counter is called by the module
 * during initialization to create a template for generating Stats::Counters with the given name and
 * labels during the lifecycle of the module.
 *
 * @param config_envoy_ptr is the pointer to the DynamicModuleBootstrapExtensionConfig in which the
 * counter will be defined.
 * @param name is the name of the counter to be defined.
 * @param label_names is the labels of the counter to be defined.
 * NOTE: label names could be null if the label_names_length is 0.
 * @param label_names_length is the length of the label_names.
 * NOTE: label_names_length could be 0 if there are no labels.
 * @param counter_id_ptr where the opaque ID that represents a unique metric will be stored. This
 * can be passed to envoy_dynamic_module_callback_bootstrap_extension_config_increment_counter
 * together with config_envoy_ptr.
 * @return the result of the operation.
 */
envoy_dynamic_module_type_metrics_result
envoy_dynamic_module_callback_bootstrap_extension_config_define_counter(
    envoy_dynamic_module_type_bootstrap_extension_config_envoy_ptr config_envoy_ptr,
    envoy_dynamic_module_type_module_buffer name,
    envoy_dynamic_module_type_module_buffer* label_names, size_t label_names_length,
    size_t* counter_id_ptr);

/**
 * envoy_dynamic_module_callback_bootstrap_extension_config_increment_counter is called by the
 * module to increment a previously defined counter.
 *
 * @param config_envoy_ptr is the pointer to the DynamicModuleBootstrapExtensionConfig object.
 * @param id is the ID of the counter previously defined using the config.
 * @param label_values is the values of the labels to be incremented.
 * NOTE: label_values could be null if the label_values_length is 0.
 * @param label_values_length is the length of the label_values.
 * NOTE: label_values_length could be 0 if there are no labels. **THE LENGTH MUST MATCH THE
 * LABEL NAMES DEFINED DURING COUNTER DEFINITION.**
 * @param value is the value to increment the counter by.
 * @return the result of the operation.
 */
envoy_dynamic_module_type_metrics_result
envoy_dynamic_module_callback_bootstrap_extension_config_increment_counter(
    envoy_dynamic_module_type_bootstrap_extension_config_envoy_ptr config_envoy_ptr, size_t id,
    envoy_dynamic_module_type_module_buffer* label_values, size_t label_values_length,
    uint64_t value);

/**
 * envoy_dynamic_module_callback_bootstrap_extension_config_define_gauge is called by the module
 * during initialization to create a template for generating Stats::Gauges with the given name and
 * labels during the lifecycle of the module.
 *
 * @param config_envoy_ptr is the pointer to the DynamicModuleBootstrapExtensionConfig in which the
 * gauge will be defined.
 * @param name is the name of the gauge to be defined.
 * @param label_names is the labels of the gauge to be defined.
 * NOTE: label names could be null if the label_names_length is 0.
 * @param label_names_length is the length of the label_names.
 * NOTE: label_names_length could be 0 if there are no labels.
 * @param gauge_id_ptr where the opaque ID that represents a unique metric will be stored. This can
 * be passed to envoy_dynamic_module_callback_bootstrap_extension_config_increment_gauge together
 * with config_envoy_ptr.
 * @return the result of the operation.
 */
envoy_dynamic_module_type_metrics_result
envoy_dynamic_module_callback_bootstrap_extension_config_define_gauge(
    envoy_dynamic_module_type_bootstrap_extension_config_envoy_ptr config_envoy_ptr,
    envoy_dynamic_module_type_module_buffer name,
    envoy_dynamic_module_type_module_buffer* label_names, size_t label_names_length,
    size_t* gauge_id_ptr);

/**
 * envoy_dynamic_module_callback_bootstrap_extension_config_set_gauge is called by the module to
 * set the value of a previously defined gauge.
 *
 * @param config_envoy_ptr is the pointer to the DynamicModuleBootstrapExtensionConfig object.
 * @param id is the ID of the gauge previously defined using the config.
 * @param label_values is the values of the labels to be set.
 * NOTE: label_values could be null if the label_values_length is 0.
 * @param label_values_length is the length of the label_values.
 * NOTE: label_values_length could be 0 if there are no labels. **THE LENGTH MUST MATCH THE
 * LABEL NAMES DEFINED DURING GAUGE DEFINITION.**
 * @param value is the value to set the gauge to.
 * @return the result of the operation.
 */
envoy_dynamic_module_type_metrics_result
envoy_dynamic_module_callback_bootstrap_extension_config_set_gauge(
    envoy_dynamic_module_type_bootstrap_extension_config_envoy_ptr config_envoy_ptr, size_t id,
    envoy_dynamic_module_type_module_buffer* label_values, size_t label_values_length,
    uint64_t value);

/**
 * envoy_dynamic_module_callback_bootstrap_extension_config_increment_gauge is called by the module
 * to increase the value of a previously defined gauge.
 *
 * @param config_envoy_ptr is the pointer to the DynamicModuleBootstrapExtensionConfig object.
 * @param id is the ID of the gauge previously defined using the config.
 * @param label_values is the values of the labels to be increased.
 * NOTE: label_values could be null if the label_values_length is 0.
 * @param label_values_length is the length of the label_values.
 * NOTE: label_values_length could be 0 if there are no labels. **THE LENGTH MUST MATCH THE
 * LABEL NAMES DEFINED DURING GAUGE DEFINITION.**
 * @param value is the value to increase the gauge by.
 * @return the result of the operation.
 */
envoy_dynamic_module_type_metrics_result
envoy_dynamic_module_callback_bootstrap_extension_config_increment_gauge(
    envoy_dynamic_module_type_bootstrap_extension_config_envoy_ptr config_envoy_ptr, size_t id,
    envoy_dynamic_module_type_module_buffer* label_values, size_t label_values_length,
    uint64_t value);

/**
 * envoy_dynamic_module_callback_bootstrap_extension_config_decrement_gauge is called by the module
 * to decrease the value of a previously defined gauge.
 *
 * @param config_envoy_ptr is the pointer to the DynamicModuleBootstrapExtensionConfig object.
 * @param id is the ID of the gauge previously defined using the config.
 * @param label_values is the values of the labels to be decreased.
 * NOTE: label_values could be null if the label_values_length is 0.
 * @param label_values_length is the length of the label_values.
 * NOTE: label_values_length could be 0 if there are no labels. **THE LENGTH MUST MATCH THE
 * LABEL NAMES DEFINED DURING GAUGE DEFINITION.**
 * @param value is the value to decrease the gauge by.
 * @return the result of the operation.
 */
envoy_dynamic_module_type_metrics_result
envoy_dynamic_module_callback_bootstrap_extension_config_decrement_gauge(
    envoy_dynamic_module_type_bootstrap_extension_config_envoy_ptr config_envoy_ptr, size_t id,
    envoy_dynamic_module_type_module_buffer* label_values, size_t label_values_length,
    uint64_t value);

/**
 * envoy_dynamic_module_callback_bootstrap_extension_config_define_histogram is called by the
 * module during initialization to create a template for generating Stats::Histograms with the given
 * name and labels during the lifecycle of the module.
 *
 * @param config_envoy_ptr is the pointer to the DynamicModuleBootstrapExtensionConfig in which the
 * histogram will be defined.
 * @param name is the name of the histogram to be defined.
 * @param label_names is the labels of the histogram to be defined.
 * NOTE: label names could be null if the label_names_length is 0.
 * @param label_names_length is the length of the label_names.
 * NOTE: label_names_length could be 0 if there are no labels.
 * @param histogram_id_ptr where the opaque ID that represents a unique metric will be stored.
 * @return the result of the operation.
 */
envoy_dynamic_module_type_metrics_result
envoy_dynamic_module_callback_bootstrap_extension_config_define_histogram(
    envoy_dynamic_module_type_bootstrap_extension_config_envoy_ptr config_envoy_ptr,
    envoy_dynamic_module_type_module_buffer name,
    envoy_dynamic_module_type_module_buffer* label_names, size_t label_names_length,
    size_t* histogram_id_ptr);

/**
 * envoy_dynamic_module_callback_bootstrap_extension_config_record_histogram_value is called by the
 * module to record a value in a previously defined histogram.
 *
 * @param config_envoy_ptr is the pointer to the DynamicModuleBootstrapExtensionConfig object.
 * @param id is the ID of the histogram previously defined using the config.
 * @param label_values is the values of the labels to be recorded.
 * NOTE: label_values could be null if the label_values_length is 0.
 * @param label_values_length is the length of the label_values.
 * NOTE: label_values_length could be 0 if there are no labels. **THE LENGTH MUST MATCH THE
 * LABEL NAMES DEFINED DURING HISTOGRAM DEFINITION.**
 * @param value is the value to record in the histogram.
 * @return the result of the operation.
 */
envoy_dynamic_module_type_metrics_result
envoy_dynamic_module_callback_bootstrap_extension_config_record_histogram_value(
    envoy_dynamic_module_type_bootstrap_extension_config_envoy_ptr config_envoy_ptr, size_t id,
    envoy_dynamic_module_type_module_buffer* label_values, size_t label_values_length,
    uint64_t value);

// -------------------- Bootstrap Extension Callbacks - Timer --------------------

/**
 * envoy_dynamic_module_callback_bootstrap_extension_timer_new is called by the module to create a
 * new timer on the main thread dispatcher. The timer is not enabled upon creation; the module must
 * call envoy_dynamic_module_callback_bootstrap_extension_timer_enable to arm it.
 *
 * When the timer fires, envoy_dynamic_module_on_bootstrap_extension_timer_fired is called on the
 * main thread.
 *
 * This must be called on the main thread.
 *
 * @param extension_config_envoy_ptr is the pointer to the DynamicModuleBootstrapExtensionConfig
 * object.
 * @return envoy_dynamic_module_type_bootstrap_extension_timer_module_ptr is the pointer to the
 * created timer.
 *
 * NOTE: it is the caller's responsibility to delete the timer using
 * envoy_dynamic_module_callback_bootstrap_extension_timer_delete when it is no longer needed.
 */
envoy_dynamic_module_type_bootstrap_extension_timer_module_ptr
envoy_dynamic_module_callback_bootstrap_extension_timer_new(
    envoy_dynamic_module_type_bootstrap_extension_config_envoy_ptr extension_config_envoy_ptr);

/**
 * envoy_dynamic_module_callback_bootstrap_extension_timer_enable is called by the module to enable
 * the timer with a given delay. If the timer is already enabled, it will be reset to the new delay.
 *
 * This must be called on the main thread.
 *
 * @param timer_ptr is the pointer to the timer created by
 * envoy_dynamic_module_callback_bootstrap_extension_timer_new.
 * @param delay_milliseconds is the delay in milliseconds before the timer fires.
 */
void envoy_dynamic_module_callback_bootstrap_extension_timer_enable(
    envoy_dynamic_module_type_bootstrap_extension_timer_module_ptr timer_ptr,
    uint64_t delay_milliseconds);

/**
 * envoy_dynamic_module_callback_bootstrap_extension_timer_disable is called by the module to
 * disable the timer without destroying it. The timer can be re-enabled later using
 * envoy_dynamic_module_callback_bootstrap_extension_timer_enable.
 *
 * This must be called on the main thread.
 *
 * @param timer_ptr is the pointer to the timer created by
 * envoy_dynamic_module_callback_bootstrap_extension_timer_new.
 */
void envoy_dynamic_module_callback_bootstrap_extension_timer_disable(
    envoy_dynamic_module_type_bootstrap_extension_timer_module_ptr timer_ptr);

/**
 * envoy_dynamic_module_callback_bootstrap_extension_timer_enabled is called by the module to check
 * whether the timer is currently armed.
 *
 * This must be called on the main thread.
 *
 * @param timer_ptr is the pointer to the timer created by
 * envoy_dynamic_module_callback_bootstrap_extension_timer_new.
 * @return true if the timer is currently enabled, false otherwise.
 */
bool envoy_dynamic_module_callback_bootstrap_extension_timer_enabled(
    envoy_dynamic_module_type_bootstrap_extension_timer_module_ptr timer_ptr);

/**
 * envoy_dynamic_module_callback_bootstrap_extension_timer_delete is called by the module to delete
 * a timer created by envoy_dynamic_module_callback_bootstrap_extension_timer_new. The timer is
 * automatically disabled before deletion.
 *
 * This must be called on the main thread.
 *
 * @param timer_ptr is the pointer to the timer created by
 * envoy_dynamic_module_callback_bootstrap_extension_timer_new.
 */
void envoy_dynamic_module_callback_bootstrap_extension_timer_delete(
    envoy_dynamic_module_type_bootstrap_extension_timer_module_ptr timer_ptr);

// -------------------- Bootstrap Extension Callbacks - File Watcher --------------------

/**
 * envoy_dynamic_module_callback_bootstrap_extension_file_watcher_add_watch is called by the module
 * to watch a file or directory. Each call creates a new watcher for the given path. The watcher
 * lifetime is managed by Envoy and tied to the config — all watchers are automatically destroyed
 * when the config is destroyed.
 *
 * When the watched path changes, envoy_dynamic_module_on_bootstrap_extension_file_changed is called
 * on the main thread with the path and events.
 *
 * This must be called on the main thread.
 *
 * @param extension_config_envoy_ptr is the pointer to the DynamicModuleBootstrapExtensionConfig
 * object.
 * @param path is the path to the file or directory to watch.
 * @param events is the bitmask of events to watch for (MovedTo = 0x1, Modified = 0x2).
 * @return true if the watch was successfully added, false otherwise (e.g. file does not exist).
 */
bool envoy_dynamic_module_callback_bootstrap_extension_file_watcher_add_watch(
    envoy_dynamic_module_type_bootstrap_extension_config_envoy_ptr extension_config_envoy_ptr,
    envoy_dynamic_module_type_module_buffer path, uint32_t events);

// -------------------- Bootstrap Extension Callbacks - Admin Handler --------------------

/**
 * envoy_dynamic_module_on_bootstrap_extension_admin_request is called when an admin endpoint
 * registered via envoy_dynamic_module_callback_bootstrap_extension_register_admin_handler is
 * requested.
 *
 * The module should use envoy_dynamic_module_callback_bootstrap_extension_admin_set_response
 * from within this hook to set the response body. Envoy copies the buffer immediately, so the
 * module does not need to keep the buffer alive after the callback returns.
 *
 * @param extension_config_envoy_ptr is the pointer to the DynamicModuleBootstrapExtensionConfig
 * object.
 * @param extension_config_module_ptr is the pointer to the in-module bootstrap extension
 * configuration created by envoy_dynamic_module_on_bootstrap_extension_config_new.
 * @param method is the HTTP method of the request (e.g. "GET", "POST").
 * @param path is the full path and query string of the request.
 * @param body is the request body. May be empty.
 * @return the HTTP status code to send back to the client. This corresponds to the numeric
 * value of the HTTP status code (e.g. 200, 404, 500).
 */
uint32_t envoy_dynamic_module_on_bootstrap_extension_admin_request(
    envoy_dynamic_module_type_bootstrap_extension_config_envoy_ptr extension_config_envoy_ptr,
    envoy_dynamic_module_type_bootstrap_extension_config_module_ptr extension_config_module_ptr,
    envoy_dynamic_module_type_envoy_buffer method, envoy_dynamic_module_type_envoy_buffer path,
    envoy_dynamic_module_type_envoy_buffer body);

/**
 * envoy_dynamic_module_callback_bootstrap_extension_admin_set_response is called by the module
 * during envoy_dynamic_module_on_bootstrap_extension_admin_request to set the response body.
 * Envoy copies the provided buffer immediately, so the module does not need to keep the buffer
 * alive after this call returns.
 *
 * This must only be called from within the on_bootstrap_extension_admin_request event hook.
 *
 * @param extension_config_envoy_ptr is the pointer to the DynamicModuleBootstrapExtensionConfig
 * object.
 * @param response_body is the response body owned by the module.
 */
void envoy_dynamic_module_callback_bootstrap_extension_admin_set_response(
    envoy_dynamic_module_type_bootstrap_extension_config_envoy_ptr extension_config_envoy_ptr,
    envoy_dynamic_module_type_module_buffer response_body);

/**
 * envoy_dynamic_module_callback_bootstrap_extension_register_admin_handler is called by the
 * module to register a custom admin HTTP endpoint. When the endpoint is requested, the
 * envoy_dynamic_module_on_bootstrap_extension_admin_request event hook will be invoked.
 *
 * This must be called on the main thread (e.g. during on_server_initialized or from a
 * scheduled event on the main thread).
 *
 * @param extension_config_envoy_ptr is the pointer to the DynamicModuleBootstrapExtensionConfig
 * object.
 * @param path_prefix is the URL prefix to handle (e.g. "/admin/my_module/status").
 * @param help_text is the help text for the handler displayed in the admin console.
 * @param removable if true, allows the handler to be removed later via
 * envoy_dynamic_module_callback_bootstrap_extension_remove_admin_handler.
 * @param mutates_server_state if true, indicates the handler mutates server state (e.g. POST
 * endpoints).
 * @return true if the handler was successfully registered, false otherwise (e.g. if admin is
 * not available or the prefix is already taken).
 */
bool envoy_dynamic_module_callback_bootstrap_extension_register_admin_handler(
    envoy_dynamic_module_type_bootstrap_extension_config_envoy_ptr extension_config_envoy_ptr,
    envoy_dynamic_module_type_module_buffer path_prefix,
    envoy_dynamic_module_type_module_buffer help_text, bool removable, bool mutates_server_state);

/**
 * envoy_dynamic_module_callback_bootstrap_extension_remove_admin_handler is called by the
 * module to remove a previously registered admin HTTP endpoint.
 *
 * This must be called on the main thread.
 *
 * @param extension_config_envoy_ptr is the pointer to the DynamicModuleBootstrapExtensionConfig
 * object.
 * @param path_prefix is the URL prefix of the handler to remove.
 * @return true if the handler was successfully removed, false otherwise (e.g. if the handler
 * was not found or is not removable).
 */
bool envoy_dynamic_module_callback_bootstrap_extension_remove_admin_handler(
    envoy_dynamic_module_type_bootstrap_extension_config_envoy_ptr extension_config_envoy_ptr,
    envoy_dynamic_module_type_module_buffer path_prefix);

// -------------------- Bootstrap Extension Callbacks - Cluster Lifecycle --------------------

/**
 * envoy_dynamic_module_callback_bootstrap_extension_enable_cluster_lifecycle is called by the
 * module to opt in to receiving cluster lifecycle events
 * (envoy_dynamic_module_on_bootstrap_extension_cluster_add_or_update and
 * envoy_dynamic_module_on_bootstrap_extension_cluster_removal).
 *
 * This must be called on the main thread, typically during or after
 * envoy_dynamic_module_on_bootstrap_extension_server_initialized, since the ClusterManager is
 * not available until that point.
 *
 * This should be called at most once. Subsequent calls are no-ops and return false.
 *
 * @param extension_config_envoy_ptr is the pointer to the DynamicModuleBootstrapExtensionConfig
 * object.
 * @return true if the cluster lifecycle callbacks were successfully registered, false otherwise.
 */
bool envoy_dynamic_module_callback_bootstrap_extension_enable_cluster_lifecycle(
    envoy_dynamic_module_type_bootstrap_extension_config_envoy_ptr extension_config_envoy_ptr);

// -------------------- Bootstrap Extension Callbacks - Listener Lifecycle --------------------

/**
 * envoy_dynamic_module_callback_bootstrap_extension_enable_listener_lifecycle is called by the
 * module to opt in to receiving listener lifecycle events
 * (envoy_dynamic_module_on_bootstrap_extension_listener_add_or_update and
 * envoy_dynamic_module_on_bootstrap_extension_listener_removal).
 *
 * This must be called on the main thread, typically during or after
 * envoy_dynamic_module_on_bootstrap_extension_server_initialized, since the ListenerManager is
 * not available until that point.
 *
 * This should be called at most once. Subsequent calls are no-ops and return false.
 *
 * @param extension_config_envoy_ptr is the pointer to the DynamicModuleBootstrapExtensionConfig
 * object.
 * @return true if the listener lifecycle callbacks were successfully registered, false otherwise.
 */
bool envoy_dynamic_module_callback_bootstrap_extension_enable_listener_lifecycle(
    envoy_dynamic_module_type_bootstrap_extension_config_envoy_ptr extension_config_envoy_ptr);

// =============================================================================
// Common Host Types (shared by cluster and standalone load balancer extensions)
// =============================================================================

/**
 * envoy_dynamic_module_type_host_health represents the health status of an upstream host.
 */
typedef enum {
  // Host is unhealthy and should not be used for traffic.
  envoy_dynamic_module_type_host_health_Unhealthy = 0,
  // Host is healthy but degraded. It can receive traffic but healthy hosts are preferred.
  envoy_dynamic_module_type_host_health_Degraded = 1,
  // Host is healthy and can receive traffic.
  envoy_dynamic_module_type_host_health_Healthy = 2,
} envoy_dynamic_module_type_host_health;

/**
 * envoy_dynamic_module_type_host_stat identifies a per-host stat.
 * These correspond to the counters and gauges in Envoy's HostStats struct.
 */
typedef enum {
  // Counter: total connection connect failures.
  envoy_dynamic_module_type_host_stat_CxConnectFail = 0,
  // Counter: total connections opened.
  envoy_dynamic_module_type_host_stat_CxTotal = 1,
  // Counter: total request errors (used for EDS load reporting).
  envoy_dynamic_module_type_host_stat_RqError = 2,
  // Counter: total successful requests (used for EDS load reporting).
  envoy_dynamic_module_type_host_stat_RqSuccess = 3,
  // Counter: total request timeouts.
  envoy_dynamic_module_type_host_stat_RqTimeout = 4,
  // Counter: total requests sent.
  envoy_dynamic_module_type_host_stat_RqTotal = 5,
  // Gauge: current active connections.
  envoy_dynamic_module_type_host_stat_CxActive = 6,
  // Gauge: current active requests.
  envoy_dynamic_module_type_host_stat_RqActive = 7,
} envoy_dynamic_module_type_host_stat;

// =============================================================================
// ============================ Cluster Extension ==============================
// =============================================================================

// =============================================================================
// Cluster Dynamic Module Types
// =============================================================================

/**
 * envoy_dynamic_module_type_cluster_config_envoy_ptr is a raw pointer to the cluster configuration
 * object in Envoy. This is passed to the module when creating a new in-module cluster
 * configuration.
 *
 * This has 1:1 correspondence with envoy_dynamic_module_type_cluster_config_module_ptr in the
 * module.
 *
 * OWNERSHIP: Envoy owns the pointer.
 */
typedef void* envoy_dynamic_module_type_cluster_config_envoy_ptr;

/**
 * envoy_dynamic_module_type_cluster_config_module_ptr is a pointer to an in-module cluster
 * configuration corresponding to an Envoy cluster configuration. The config is responsible for
 * creating new cluster instances.
 *
 * This has 1:1 correspondence with envoy_dynamic_module_type_cluster_config_envoy_ptr in Envoy.
 *
 * OWNERSHIP: The module is responsible for managing the lifetime of the pointer. The pointer can
 * be released when envoy_dynamic_module_on_cluster_config_destroy is called for the same pointer.
 */
typedef const void* envoy_dynamic_module_type_cluster_config_module_ptr;

/**
 * envoy_dynamic_module_type_cluster_envoy_ptr is a raw pointer to the DynamicModuleCluster class
 * in Envoy. This is passed to the module when creating a new cluster and used to access cluster
 * operations such as adding/removing hosts.
 *
 * This has 1:1 correspondence with envoy_dynamic_module_type_cluster_module_ptr in the module.
 *
 * OWNERSHIP: Envoy owns the pointer. The pointer is valid until
 * envoy_dynamic_module_on_cluster_destroy is called.
 */
typedef void* envoy_dynamic_module_type_cluster_envoy_ptr;

/**
 * envoy_dynamic_module_type_cluster_module_ptr is a pointer to an in-module cluster instance
 * corresponding to an Envoy cluster.
 *
 * This has 1:1 correspondence with envoy_dynamic_module_type_cluster_envoy_ptr in Envoy.
 *
 * OWNERSHIP: The module is responsible for managing the lifetime of the pointer. The pointer can
 * be released when envoy_dynamic_module_on_cluster_destroy is called for the same pointer.
 */
typedef const void* envoy_dynamic_module_type_cluster_module_ptr;

/**
 * envoy_dynamic_module_type_cluster_lb_envoy_ptr is a raw pointer to the load balancer instance
 * in Envoy. This provides thread-local access to the cluster's host set for load balancing
 * decisions. One load balancer instance is created per worker thread.
 *
 * This has 1:1 correspondence with envoy_dynamic_module_type_cluster_lb_module_ptr in the module.
 *
 * OWNERSHIP: Envoy owns the pointer. The pointer is valid until
 * envoy_dynamic_module_on_cluster_lb_destroy is called.
 */
typedef void* envoy_dynamic_module_type_cluster_lb_envoy_ptr;

/**
 * envoy_dynamic_module_type_cluster_lb_module_ptr is a pointer to an in-module load balancer
 * instance. The load balancer is responsible for selecting hosts for requests.
 *
 * This has 1:1 correspondence with envoy_dynamic_module_type_cluster_lb_envoy_ptr in Envoy.
 *
 * OWNERSHIP: The module is responsible for managing the lifetime of the pointer. The pointer can
 * be released when envoy_dynamic_module_on_cluster_lb_destroy is called for the same pointer.
 */
typedef const void* envoy_dynamic_module_type_cluster_lb_module_ptr;

/**
 * envoy_dynamic_module_type_cluster_host_envoy_ptr is a pointer to a Host in Envoy's cluster. This
 * represents an upstream endpoint that can receive traffic.
 *
 * OWNERSHIP: Envoy owns the pointer. The pointer remains valid as long as the host is part of the
 * cluster's host set.
 */
typedef void* envoy_dynamic_module_type_cluster_host_envoy_ptr;

/**
 * envoy_dynamic_module_type_cluster_lb_context_envoy_ptr is a pointer to the LoadBalancerContext in
 * Envoy. This provides per-request information for load balancing decisions such as hash keys and
 * downstream headers.
 *
 * OWNERSHIP: Envoy owns the pointer. The pointer is valid only during the
 * envoy_dynamic_module_on_cluster_lb_choose_host call, unless async host selection is used. When
 * async host selection is used, the context remains valid until the async completion callback is
 * called or the selection is canceled.
 */
typedef void* envoy_dynamic_module_type_cluster_lb_context_envoy_ptr;

/**
 * envoy_dynamic_module_type_cluster_scheduler_envoy_ptr is a raw pointer to the
 * DynamicModuleClusterScheduler class in Envoy. This is used to schedule events to the main thread
 * from background threads.
 *
 * OWNERSHIP: The allocation is done by Envoy but the module is responsible for managing the
 * lifetime of the pointer. Notably, it must be explicitly destroyed by the module
 * when scheduling cluster events is done. The creation of this pointer is done by
 * envoy_dynamic_module_callback_cluster_scheduler_new and the scheduling and destruction is done by
 * envoy_dynamic_module_callback_cluster_scheduler_commit and
 * envoy_dynamic_module_callback_cluster_scheduler_delete. Since its lifecycle is owned/managed by
 * the module, this has _module_ptr suffix.
 */
typedef void* envoy_dynamic_module_type_cluster_scheduler_module_ptr;

/**
 * envoy_dynamic_module_type_cluster_lb_async_handle_module_ptr is a pointer to an in-module async
 * host selection handle. This is returned by the module during
 * envoy_dynamic_module_on_cluster_lb_choose_host when the module needs to perform asynchronous
 * work (e.g., DNS resolution) before selecting a host.
 *
 * OWNERSHIP: The module owns the pointer. The module must keep it valid until either
 * envoy_dynamic_module_callback_cluster_lb_async_host_selection_complete is called or
 * envoy_dynamic_module_on_cluster_lb_cancel_host_selection is called. After either event, the
 * module may release the handle.
 */
typedef void* envoy_dynamic_module_type_cluster_lb_async_handle_module_ptr;

// =============================================================================
// Cluster Event Hooks
// =============================================================================

/**
 * envoy_dynamic_module_on_cluster_config_new is called by the main thread when a cluster
 * configuration referencing this module is loaded. The module should parse the configuration and
 * return a pointer to the in-module configuration object.
 *
 * @param config_envoy_ptr is the pointer to the Envoy cluster configuration object.
 * @param name is the cluster name identifying the implementation within the module.
 * @param config is the configuration bytes for the module.
 * @return envoy_dynamic_module_type_cluster_config_module_ptr is the pointer to the in-module
 * cluster configuration. Returning nullptr indicates a failure, and the cluster configuration
 * will be rejected.
 */
envoy_dynamic_module_type_cluster_config_module_ptr envoy_dynamic_module_on_cluster_config_new(
    envoy_dynamic_module_type_cluster_config_envoy_ptr config_envoy_ptr,
    envoy_dynamic_module_type_envoy_buffer name, envoy_dynamic_module_type_envoy_buffer config);

/**
 * envoy_dynamic_module_on_cluster_config_destroy is called when the cluster configuration is
 * destroyed. The module should release any resources associated with the configuration.
 *
 * @param config_module_ptr is the pointer to the in-module cluster configuration.
 */
void envoy_dynamic_module_on_cluster_config_destroy(
    envoy_dynamic_module_type_cluster_config_module_ptr config_module_ptr);

/**
 * envoy_dynamic_module_on_cluster_new is called when a new cluster instance is created.
 *
 * @param config_module_ptr is the pointer to the in-module cluster configuration.
 * @param cluster_envoy_ptr is the pointer to the Envoy cluster object, which can be used with
 * cluster callbacks such as envoy_dynamic_module_callback_cluster_add_hosts.
 * @return envoy_dynamic_module_type_cluster_module_ptr is the pointer to the in-module cluster.
 * Returning nullptr indicates a failure.
 */
envoy_dynamic_module_type_cluster_module_ptr envoy_dynamic_module_on_cluster_new(
    envoy_dynamic_module_type_cluster_config_module_ptr config_module_ptr,
    envoy_dynamic_module_type_cluster_envoy_ptr cluster_envoy_ptr);

/**
 * envoy_dynamic_module_on_cluster_init is called when cluster initialization begins. The module
 * should perform initial host discovery and call
 * envoy_dynamic_module_callback_cluster_pre_init_complete when the initial set of hosts is ready.
 *
 * @param cluster_envoy_ptr is the pointer to the Envoy cluster object.
 * @param cluster_module_ptr is the pointer to the in-module cluster.
 */
void envoy_dynamic_module_on_cluster_init(
    envoy_dynamic_module_type_cluster_envoy_ptr cluster_envoy_ptr,
    envoy_dynamic_module_type_cluster_module_ptr cluster_module_ptr);

/**
 * envoy_dynamic_module_on_cluster_destroy is called when the cluster is destroyed. The module
 * should release any resources associated with the cluster.
 *
 * @param cluster_module_ptr is the pointer to the in-module cluster.
 */
void envoy_dynamic_module_on_cluster_destroy(
    envoy_dynamic_module_type_cluster_module_ptr cluster_module_ptr);

/**
 * envoy_dynamic_module_on_cluster_lb_new is called when a new load balancer instance is created
 * for a worker thread. Each worker thread gets its own load balancer instance.
 *
 * @param cluster_module_ptr is the pointer to the in-module cluster.
 * @param lb_envoy_ptr is the pointer to the Envoy load balancer object, which provides
 * thread-local access to the cluster's host set via callbacks such as
 * envoy_dynamic_module_callback_cluster_lb_get_healthy_host_count.
 * @return envoy_dynamic_module_type_cluster_lb_module_ptr is the pointer to the in-module load
 * balancer. Returning nullptr indicates a failure.
 */
envoy_dynamic_module_type_cluster_lb_module_ptr envoy_dynamic_module_on_cluster_lb_new(
    envoy_dynamic_module_type_cluster_module_ptr cluster_module_ptr,
    envoy_dynamic_module_type_cluster_lb_envoy_ptr lb_envoy_ptr);

/**
 * envoy_dynamic_module_on_cluster_lb_destroy is called when the load balancer is destroyed.
 *
 * @param lb_module_ptr is the pointer to the in-module load balancer.
 */
void envoy_dynamic_module_on_cluster_lb_destroy(
    envoy_dynamic_module_type_cluster_lb_module_ptr lb_module_ptr);

/**
 * envoy_dynamic_module_on_cluster_lb_choose_host is called to select a host for a request.
 *
 * The module can respond in one of three ways:
 * 1. Synchronous success: Set ``*host_out`` to a valid host pointer and ``*async_handle_out`` to
 *    nullptr.
 * 2. Synchronous failure: Set ``*host_out`` to nullptr and ``*async_handle_out`` to nullptr.
 * 3. Async pending: Set ``*host_out`` to nullptr and ``*async_handle_out`` to a valid in-module
 *    async handle. In this case, the module must later call
 *    envoy_dynamic_module_callback_cluster_lb_async_host_selection_complete to deliver the result,
 *    unless envoy_dynamic_module_on_cluster_lb_cancel_host_selection is called first.
 *
 * @param lb_module_ptr is the pointer to the in-module load balancer.
 * @param context_envoy_ptr is the per-request load balancer context. Can be nullptr.
 * @param host_out is the output pointer for the selected host. Set to nullptr if no host is
 * immediately available.
 * @param async_handle_out is the output pointer for the async handle. Set to nullptr for
 * synchronous results.
 */
void envoy_dynamic_module_on_cluster_lb_choose_host(
    envoy_dynamic_module_type_cluster_lb_module_ptr lb_module_ptr,
    envoy_dynamic_module_type_cluster_lb_context_envoy_ptr context_envoy_ptr,
    envoy_dynamic_module_type_cluster_host_envoy_ptr* host_out,
    envoy_dynamic_module_type_cluster_lb_async_handle_module_ptr* async_handle_out);

/**
 * envoy_dynamic_module_on_cluster_lb_cancel_host_selection is called when the stream is destroyed
 * before async host selection completes (e.g., stream timeout). After this call, the module must
 * not call envoy_dynamic_module_callback_cluster_lb_async_host_selection_complete for this handle.
 *
 * This is optional. Only modules that use asynchronous host selection need to implement this.
 *
 * @param lb_module_ptr is the pointer to the in-module load balancer.
 * @param async_handle_module_ptr is the in-module async handle that was previously returned from
 * envoy_dynamic_module_on_cluster_lb_choose_host.
 */
void envoy_dynamic_module_on_cluster_lb_cancel_host_selection(
    envoy_dynamic_module_type_cluster_lb_module_ptr lb_module_ptr,
    envoy_dynamic_module_type_cluster_lb_async_handle_module_ptr async_handle_module_ptr);

/**
 * envoy_dynamic_module_on_cluster_scheduled is called on the main thread when an event previously
 * scheduled via envoy_dynamic_module_callback_cluster_scheduler_commit is dispatched. The module
 * can use the event_id to distinguish between different scheduled events.
 *
 * This is optional. Only modules that use the scheduler need to implement this.
 *
 * @param cluster_envoy_ptr is the pointer to the Envoy cluster object. This can be used with
 * cluster callbacks such as envoy_dynamic_module_callback_cluster_add_hosts.
 * @param cluster_module_ptr is the pointer to the in-module cluster.
 * @param event_id is the ID of the event that was scheduled with
 * envoy_dynamic_module_callback_cluster_scheduler_commit.
 */
void envoy_dynamic_module_on_cluster_scheduled(
    envoy_dynamic_module_type_cluster_envoy_ptr cluster_envoy_ptr,
    envoy_dynamic_module_type_cluster_module_ptr cluster_module_ptr, uint64_t event_id);

/**
 * envoy_dynamic_module_on_cluster_server_initialized is called when the server initialization is
 * complete. This is called on the main thread during the PostInit lifecycle stage, after all
 * clusters have finished initialization and before workers are started.
 *
 * This is the appropriate place to start background discovery tasks or establish connections that
 * depend on the server being fully operational.
 *
 * @param cluster_envoy_ptr is the pointer to the Envoy cluster object.
 * @param cluster_module_ptr is the pointer to the in-module cluster.
 */
void envoy_dynamic_module_on_cluster_server_initialized(
    envoy_dynamic_module_type_cluster_envoy_ptr cluster_envoy_ptr,
    envoy_dynamic_module_type_cluster_module_ptr cluster_module_ptr);

/**
 * envoy_dynamic_module_on_cluster_drain_started is called when Envoy begins draining.
 *
 * This is called on the main thread before workers are stopped. The module can still use cluster
 * operations during drain. This is the appropriate place to stop accepting new hosts, close
 * persistent connections, or de-register from service discovery.
 *
 * @param cluster_envoy_ptr is the pointer to the Envoy cluster object.
 * @param cluster_module_ptr is the pointer to the in-module cluster.
 */
void envoy_dynamic_module_on_cluster_drain_started(
    envoy_dynamic_module_type_cluster_envoy_ptr cluster_envoy_ptr,
    envoy_dynamic_module_type_cluster_module_ptr cluster_module_ptr);

/**
 * envoy_dynamic_module_on_cluster_shutdown is called when Envoy is about to exit.
 *
 * This is called on the main thread during the ShutdownExit lifecycle stage. The module MUST
 * invoke the completion callback exactly once with the provided context when it has finished
 * cleanup. Envoy will wait for the callback before terminating. This is the appropriate place to
 * flush batched data, close gRPC connections, or signal external systems.
 *
 * @param cluster_envoy_ptr is the pointer to the Envoy cluster object.
 * @param cluster_module_ptr is the pointer to the in-module cluster.
 * @param completion_callback is the callback that must be invoked when shutdown cleanup is done.
 * @param completion_context is the opaque context pointer to pass to the completion callback.
 */
void envoy_dynamic_module_on_cluster_shutdown(
    envoy_dynamic_module_type_cluster_envoy_ptr cluster_envoy_ptr,
    envoy_dynamic_module_type_cluster_module_ptr cluster_module_ptr,
    envoy_dynamic_module_type_event_cb completion_callback, void* completion_context);

/**
 * envoy_dynamic_module_on_cluster_http_callout_done is called on the main thread when an HTTP
 * callout initiated by envoy_dynamic_module_callback_cluster_http_callout receives a response or
 * fails.
 *
 * This is optional. Only modules that use HTTP callouts need to implement this. If not
 * implemented, envoy_dynamic_module_callback_cluster_http_callout will return
 * envoy_dynamic_module_type_http_callout_init_result_CannotCreateRequest.
 *
 * @param cluster_envoy_ptr is the pointer to the Envoy cluster object.
 * @param cluster_module_ptr is the pointer to the in-module cluster.
 * @param callout_id is the ID of the callout that was returned by
 * envoy_dynamic_module_callback_cluster_http_callout.
 * @param result is the result of the HTTP callout.
 * @param headers is the response headers. Can be nullptr if the callout failed.
 * @param headers_size is the number of response headers.
 * @param body_chunks is the response body chunks. Can be nullptr if the callout failed or
 * there is no body.
 * @param body_chunks_size is the number of response body chunks.
 */
void envoy_dynamic_module_on_cluster_http_callout_done(
    envoy_dynamic_module_type_cluster_envoy_ptr cluster_envoy_ptr,
    envoy_dynamic_module_type_cluster_module_ptr cluster_module_ptr, uint64_t callout_id,
    envoy_dynamic_module_type_http_callout_result result,
    envoy_dynamic_module_type_envoy_http_header* headers, size_t headers_size,
    envoy_dynamic_module_type_envoy_buffer* body_chunks, size_t body_chunks_size);

// =============================================================================
// Cluster Dynamic Module Callbacks
// =============================================================================

/**
 * envoy_dynamic_module_callback_cluster_add_hosts adds multiple hosts to the cluster at the
 * specified priority level with locality and metadata information in a single batch operation.
 *
 * This triggers only one priority set update regardless of how many hosts are added.
 *
 * For simple use cases that do not require locality or priority, the SDK provides convenience
 * wrappers that pass empty locality, no metadata, and priority 0.
 *
 * @param cluster_envoy_ptr is the pointer to the Envoy cluster.
 * @param priority is the priority level to add hosts to. Use 0 for the default priority.
 * @param addresses is the array of host addresses in ``ip:port`` format (e.g.,
 * ``127.0.0.1:8080``). Each address is owned by the module.
 * @param weights is the array of load balancing weights for each host (1-128).
 * @param regions is the array of locality region strings for each host. Each entry is owned by the
 * module. An entry with length 0 indicates no region.
 * @param zones is the array of locality zone strings for each host. Each entry is owned by the
 * module. An entry with length 0 indicates no zone.
 * @param sub_zones is the array of locality sub-zone strings for each host. Each entry is owned by
 * the module. An entry with length 0 indicates no sub-zone.
 * @param metadata_pairs is an optional flat array of (filter_name, key, value) triples for
 * endpoint metadata. Each triple consists of three consecutive
 * ``envoy_dynamic_module_type_module_buffer`` entries. All metadata values are stored as string
 * values. Can be nullptr if no metadata is needed.
 * @param metadata_pairs_per_host is the number of (filter_name, key, value) triples per host. Must
 * be the same for all hosts. The total number of entries in ``metadata_pairs`` must be
 * ``count * metadata_pairs_per_host * 3``. Can be 0 if no metadata is needed.
 * @param count is the number of hosts to add. Must match the length of all per-host arrays.
 * @param result_host_ptrs is the output array of host pointers. On success, each entry is set to
 * the corresponding created host pointer. On failure, the array contents are undefined. The array
 * must be pre-allocated by the caller with at least ``count`` entries.
 * @return true if all hosts were added successfully, false if any host failed (e.g., invalid
 * address or weight). On failure, no hosts are added.
 */
bool envoy_dynamic_module_callback_cluster_add_hosts(
    envoy_dynamic_module_type_cluster_envoy_ptr cluster_envoy_ptr, uint32_t priority,
    const envoy_dynamic_module_type_module_buffer* addresses, const uint32_t* weights,
    const envoy_dynamic_module_type_module_buffer* regions,
    const envoy_dynamic_module_type_module_buffer* zones,
    const envoy_dynamic_module_type_module_buffer* sub_zones,
    const envoy_dynamic_module_type_module_buffer* metadata_pairs, size_t metadata_pairs_per_host,
    size_t count, envoy_dynamic_module_type_cluster_host_envoy_ptr* result_host_ptrs);

/**
 * envoy_dynamic_module_callback_cluster_remove_hosts removes multiple hosts from the cluster in a
 * single batch operation. This triggers only one priority set update regardless of how many hosts
 * are removed.
 *
 * @param cluster_envoy_ptr is the pointer to the Envoy cluster.
 * @param host_envoy_ptrs is the array of host pointers to remove, as returned by
 * envoy_dynamic_module_callback_cluster_add_hosts.
 * @param count is the number of hosts to remove.
 * @return the number of hosts that were successfully removed. Hosts not found in the cluster are
 * skipped.
 */
size_t envoy_dynamic_module_callback_cluster_remove_hosts(
    envoy_dynamic_module_type_cluster_envoy_ptr cluster_envoy_ptr,
    const envoy_dynamic_module_type_cluster_host_envoy_ptr* host_envoy_ptrs, size_t count);

/**
 * envoy_dynamic_module_callback_cluster_update_host_health updates the health status of a host in
 * the cluster. This allows the module to mark hosts as unhealthy, degraded, or healthy based on
 * external health information (e.g., from a custom service discovery system).
 *
 * This uses EDS health flags internally and triggers a priority set update so that the load
 * balancer sees the change.
 *
 * This is optional. Only modules that manage host health externally need to use this.
 *
 * @param cluster_envoy_ptr is the pointer to the Envoy cluster.
 * @param host_envoy_ptr is the pointer to the host to update, as returned by
 * envoy_dynamic_module_callback_cluster_add_hosts.
 * @param health_status is the new health status for the host.
 * @return true if the host was found and updated, false if the host pointer is not in the cluster.
 */
bool envoy_dynamic_module_callback_cluster_update_host_health(
    envoy_dynamic_module_type_cluster_envoy_ptr cluster_envoy_ptr,
    envoy_dynamic_module_type_cluster_host_envoy_ptr host_envoy_ptr,
    envoy_dynamic_module_type_host_health health_status);

/**
 * envoy_dynamic_module_callback_cluster_find_host_by_address looks up a host by its address string
 * across all priorities in the cluster and returns the host pointer. This provides O(1) lookup by
 * address using the cross-priority host map.
 *
 * The address string must match the format ``ip:port`` (e.g., ``10.0.0.1:8080``).
 *
 * This is optional. Only modules that need to resolve addresses to host pointers need to use this.
 *
 * @param cluster_envoy_ptr is the pointer to the Envoy cluster.
 * @param address is the address string to look up, owned by the module.
 * @return the host pointer if found, or nullptr if the address is not in the cluster.
 */
envoy_dynamic_module_type_cluster_host_envoy_ptr
envoy_dynamic_module_callback_cluster_find_host_by_address(
    envoy_dynamic_module_type_cluster_envoy_ptr cluster_envoy_ptr,
    envoy_dynamic_module_type_module_buffer address);

/**
 * envoy_dynamic_module_callback_cluster_pre_init_complete signals that the cluster's initial host
 * discovery is complete. The module must call this during or after
 * envoy_dynamic_module_on_cluster_init to allow Envoy to start routing traffic to this cluster.
 *
 * @param cluster_envoy_ptr is the pointer to the Envoy cluster.
 */
void envoy_dynamic_module_callback_cluster_pre_init_complete(
    envoy_dynamic_module_type_cluster_envoy_ptr cluster_envoy_ptr);

/**
 * envoy_dynamic_module_callback_cluster_lb_get_healthy_host_count returns the number of healthy
 * hosts at the given priority level in the cluster's host set.
 *
 * @param lb_envoy_ptr is the pointer to the Envoy load balancer.
 * @param priority is the priority level (typically 0 for default priority).
 * @return the number of healthy hosts at the given priority level.
 */
size_t envoy_dynamic_module_callback_cluster_lb_get_healthy_host_count(
    envoy_dynamic_module_type_cluster_lb_envoy_ptr lb_envoy_ptr, uint32_t priority);

/**
 * envoy_dynamic_module_callback_cluster_lb_get_healthy_host returns a healthy host pointer by
 * index at the given priority level.
 *
 * @param lb_envoy_ptr is the pointer to the Envoy load balancer.
 * @param priority is the priority level.
 * @param index is the index of the host in the healthy host list.
 * @return envoy_dynamic_module_type_cluster_host_envoy_ptr is the pointer to the host,
 * or nullptr if the index is out of bounds.
 */
envoy_dynamic_module_type_cluster_host_envoy_ptr
envoy_dynamic_module_callback_cluster_lb_get_healthy_host(
    envoy_dynamic_module_type_cluster_lb_envoy_ptr lb_envoy_ptr, uint32_t priority, size_t index);

// =============================================================================
// Cluster LB Host Information Callbacks
// =============================================================================
//
// These callbacks provide the cluster load balancer with access to host
// information from the cluster's priority set. They mirror the standalone load
// balancer's host information callbacks (envoy_dynamic_module_callback_lb_*)
// but operate on the cluster_lb_envoy_ptr instead.

/**
 * envoy_dynamic_module_callback_cluster_lb_get_cluster_name returns the name of the cluster.
 *
 * @param lb_envoy_ptr is the pointer to the Envoy cluster load balancer.
 * @param result is the output for the cluster name. The buffer is owned by Envoy.
 */
void envoy_dynamic_module_callback_cluster_lb_get_cluster_name(
    envoy_dynamic_module_type_cluster_lb_envoy_ptr lb_envoy_ptr,
    envoy_dynamic_module_type_envoy_buffer* result);

/**
 * envoy_dynamic_module_callback_cluster_lb_get_hosts_count returns the number of all hosts
 * at a given priority level, regardless of health status.
 *
 * @param lb_envoy_ptr is the pointer to the Envoy cluster load balancer.
 * @param priority is the priority level to query.
 * @return the number of all hosts at the given priority.
 */
size_t envoy_dynamic_module_callback_cluster_lb_get_hosts_count(
    envoy_dynamic_module_type_cluster_lb_envoy_ptr lb_envoy_ptr, uint32_t priority);

/**
 * envoy_dynamic_module_callback_cluster_lb_get_degraded_hosts_count returns the number of degraded
 * hosts at a given priority level.
 *
 * @param lb_envoy_ptr is the pointer to the Envoy cluster load balancer.
 * @param priority is the priority level to query.
 * @return the number of degraded hosts at the given priority.
 */
size_t envoy_dynamic_module_callback_cluster_lb_get_degraded_hosts_count(
    envoy_dynamic_module_type_cluster_lb_envoy_ptr lb_envoy_ptr, uint32_t priority);

/**
 * envoy_dynamic_module_callback_cluster_lb_get_priority_set_size returns the number of priority
 * levels in the cluster.
 *
 * @param lb_envoy_ptr is the pointer to the Envoy cluster load balancer.
 * @return the number of priority levels.
 */
size_t envoy_dynamic_module_callback_cluster_lb_get_priority_set_size(
    envoy_dynamic_module_type_cluster_lb_envoy_ptr lb_envoy_ptr);

/**
 * envoy_dynamic_module_callback_cluster_lb_get_healthy_host_address returns the address of a host
 * by index within the healthy hosts at a given priority.
 *
 * @param lb_envoy_ptr is the pointer to the Envoy cluster load balancer.
 * @param priority is the priority level.
 * @param index is the index of the host within healthy hosts.
 * @param result is the output for the host address as a string.
 * @return true if the host was found, false otherwise.
 */
bool envoy_dynamic_module_callback_cluster_lb_get_healthy_host_address(
    envoy_dynamic_module_type_cluster_lb_envoy_ptr lb_envoy_ptr, uint32_t priority, size_t index,
    envoy_dynamic_module_type_envoy_buffer* result);

/**
 * envoy_dynamic_module_callback_cluster_lb_get_healthy_host_weight returns the load balancing
 * weight of a host by index within the healthy hosts at a given priority.
 *
 * @param lb_envoy_ptr is the pointer to the Envoy cluster load balancer.
 * @param priority is the priority level.
 * @param index is the index of the host within healthy hosts.
 * @return the weight of the host (1-128), or 0 if the host was not found.
 */
uint32_t envoy_dynamic_module_callback_cluster_lb_get_healthy_host_weight(
    envoy_dynamic_module_type_cluster_lb_envoy_ptr lb_envoy_ptr, uint32_t priority, size_t index);

/**
 * envoy_dynamic_module_callback_cluster_lb_get_host_health returns the health status of a host
 * by index within all hosts at a given priority.
 *
 * @param lb_envoy_ptr is the pointer to the Envoy cluster load balancer.
 * @param priority is the priority level.
 * @param index is the index of the host within all hosts.
 * @return the health status of the host.
 */
envoy_dynamic_module_type_host_health envoy_dynamic_module_callback_cluster_lb_get_host_health(
    envoy_dynamic_module_type_cluster_lb_envoy_ptr lb_envoy_ptr, uint32_t priority, size_t index);

/**
 * envoy_dynamic_module_callback_cluster_lb_get_host_health_by_address looks up a host by its
 * address string across all priorities and returns the health status. This uses the cross-priority
 * host map internally, providing O(1) lookup by address.
 *
 * The address string must match the format ``ip:port`` (e.g., ``10.0.0.1:8080``).
 *
 * @param lb_envoy_ptr is the pointer to the Envoy cluster load balancer.
 * @param address is the address string to look up, owned by the module.
 * @param result is the output for the health status of the host.
 * @return true if the host was found, false if the address is not in the host map.
 */
bool envoy_dynamic_module_callback_cluster_lb_get_host_health_by_address(
    envoy_dynamic_module_type_cluster_lb_envoy_ptr lb_envoy_ptr,
    envoy_dynamic_module_type_module_buffer address, envoy_dynamic_module_type_host_health* result);

/**
 * envoy_dynamic_module_callback_cluster_lb_find_host_by_address looks up a host by its address
 * string across all priorities in the cluster's priority set and returns the host pointer. This
 * uses the cross-priority host map internally, providing O(1) lookup by address.
 *
 * Unlike envoy_dynamic_module_callback_cluster_find_host_by_address which operates on the
 * cluster_envoy_ptr (main thread), this operates on the lb_envoy_ptr and is safe to call from
 * worker threads during load balancing decisions.
 *
 * The address string must match the format ``ip:port`` (e.g., ``10.0.0.1:8080``).
 *
 * This is optional. Only modules that need to resolve addresses to host pointers during load
 * balancing need to use this.
 *
 * @param lb_envoy_ptr is the pointer to the Envoy cluster load balancer.
 * @param address is the address string to look up, owned by the module.
 * @return the host pointer if found, or nullptr if the address is not in the cluster.
 */
envoy_dynamic_module_type_cluster_host_envoy_ptr
envoy_dynamic_module_callback_cluster_lb_find_host_by_address(
    envoy_dynamic_module_type_cluster_lb_envoy_ptr lb_envoy_ptr,
    envoy_dynamic_module_type_module_buffer address);

/**
 * envoy_dynamic_module_callback_cluster_lb_get_host returns a host pointer by index within all
 * hosts at the given priority level, regardless of health status.
 *
 * Unlike envoy_dynamic_module_callback_cluster_lb_get_healthy_host which only returns healthy
 * hosts, this returns any host at the given index in the full host list. This is useful for
 * modules that need to iterate over all hosts or access hosts that may not be healthy.
 *
 * This is optional. Only modules that need access to all hosts regardless of health status need
 * to use this.
 *
 * @param lb_envoy_ptr is the pointer to the Envoy cluster load balancer.
 * @param priority is the priority level.
 * @param index is the index of the host within all hosts.
 * @return envoy_dynamic_module_type_cluster_host_envoy_ptr is the pointer to the host,
 * or nullptr if the index is out of bounds.
 */
envoy_dynamic_module_type_cluster_host_envoy_ptr envoy_dynamic_module_callback_cluster_lb_get_host(
    envoy_dynamic_module_type_cluster_lb_envoy_ptr lb_envoy_ptr, uint32_t priority, size_t index);

/**
 * envoy_dynamic_module_callback_cluster_lb_get_host_address returns the address of a host
 * by index within all hosts at a given priority.
 *
 * @param lb_envoy_ptr is the pointer to the Envoy cluster load balancer.
 * @param priority is the priority level.
 * @param index is the index of the host within all hosts.
 * @param result is the output for the host address as a string.
 * @return true if the host was found, false otherwise.
 */
bool envoy_dynamic_module_callback_cluster_lb_get_host_address(
    envoy_dynamic_module_type_cluster_lb_envoy_ptr lb_envoy_ptr, uint32_t priority, size_t index,
    envoy_dynamic_module_type_envoy_buffer* result);

/**
 * envoy_dynamic_module_callback_cluster_lb_get_host_weight returns the load balancing weight
 * of a host by index within all hosts at a given priority.
 *
 * @param lb_envoy_ptr is the pointer to the Envoy cluster load balancer.
 * @param priority is the priority level.
 * @param index is the index of the host within all hosts.
 * @return the weight of the host (1-128), or 0 if the host was not found.
 */
uint32_t envoy_dynamic_module_callback_cluster_lb_get_host_weight(
    envoy_dynamic_module_type_cluster_lb_envoy_ptr lb_envoy_ptr, uint32_t priority, size_t index);

/**
 * envoy_dynamic_module_callback_cluster_lb_get_host_stat returns the value of a per-host stat
 * identified by the stat enum. This provides access to host-level counters and gauges such as
 * total connections, request errors, active requests, and active connections.
 *
 * @param lb_envoy_ptr is the pointer to the Envoy cluster load balancer.
 * @param priority is the priority level.
 * @param index is the index of the host within all hosts.
 * @param stat is the host stat to query.
 * @return the stat value, or 0 if the host was not found or the stat is invalid.
 */
uint64_t envoy_dynamic_module_callback_cluster_lb_get_host_stat(
    envoy_dynamic_module_type_cluster_lb_envoy_ptr lb_envoy_ptr, uint32_t priority, size_t index,
    envoy_dynamic_module_type_host_stat stat);

/**
 * envoy_dynamic_module_callback_cluster_lb_get_host_locality returns the locality information
 * (region, zone, sub_zone) for a host by index within all hosts at a given priority.
 * This enables zone-aware and locality-aware load balancing algorithms.
 *
 * @param lb_envoy_ptr is the pointer to the Envoy cluster load balancer.
 * @param priority is the priority level.
 * @param index is the index of the host within all hosts.
 * @param region is the output for the region string. Can be null if not needed.
 * @param zone is the output for the zone string. Can be null if not needed.
 * @param sub_zone is the output for the sub-zone string. Can be null if not needed.
 * @return true if the host was found, false otherwise.
 */
bool envoy_dynamic_module_callback_cluster_lb_get_host_locality(
    envoy_dynamic_module_type_cluster_lb_envoy_ptr lb_envoy_ptr, uint32_t priority, size_t index,
    envoy_dynamic_module_type_envoy_buffer* region, envoy_dynamic_module_type_envoy_buffer* zone,
    envoy_dynamic_module_type_envoy_buffer* sub_zone);

/**
 * envoy_dynamic_module_callback_cluster_lb_set_host_data stores a module-defined opaque value on a
 * host identified by priority and index within all hosts. This data is stored per load balancer
 * instance (i.e., per worker thread) and can be used to attach per-host state for load balancing
 * decisions such as moving averages or request tracking.
 *
 * The data is only valid for the lifetime of the load balancer instance. It is not shared across
 * worker threads. Callers are responsible for managing the memory pointed to by the stored value
 * if it represents a pointer.
 *
 * @param lb_envoy_ptr is the pointer to the Envoy cluster load balancer.
 * @param priority is the priority level.
 * @param index is the index of the host within all hosts.
 * @param data is the opaque value to store. Use 0 to clear the data.
 * @return true if the data was stored successfully, false if the host was not found.
 */
bool envoy_dynamic_module_callback_cluster_lb_set_host_data(
    envoy_dynamic_module_type_cluster_lb_envoy_ptr lb_envoy_ptr, uint32_t priority, size_t index,
    uintptr_t data);

/**
 * envoy_dynamic_module_callback_cluster_lb_get_host_data retrieves a module-defined opaque value
 * previously stored on a host via envoy_dynamic_module_callback_cluster_lb_set_host_data.
 *
 * @param lb_envoy_ptr is the pointer to the Envoy cluster load balancer.
 * @param priority is the priority level.
 * @param index is the index of the host within all hosts.
 * @param data is the output for the stored opaque value. Set to 0 if no data was stored.
 * @return true if the host was found, false otherwise.
 */
bool envoy_dynamic_module_callback_cluster_lb_get_host_data(
    envoy_dynamic_module_type_cluster_lb_envoy_ptr lb_envoy_ptr, uint32_t priority, size_t index,
    uintptr_t* data);

/**
 * envoy_dynamic_module_callback_cluster_lb_get_host_metadata_string is called by the module to get
 * the string value of a host's endpoint metadata by looking up the given filter name and key.
 * If the key does not exist or the value is not a string, this returns false.
 *
 * @param lb_envoy_ptr is the pointer to the Envoy cluster load balancer.
 * @param priority is the priority level.
 * @param index is the index of the host within all hosts.
 * @param filter_name is the filter namespace to look up (e.g., ``envoy.lb``).
 * @param key is the key within the filter namespace.
 * @param result is the output for the string value. The buffer is owned by Envoy and is valid
 * until the end of the current event hook.
 * @return true if the key was found and the value is a string, false otherwise.
 */
bool envoy_dynamic_module_callback_cluster_lb_get_host_metadata_string(
    envoy_dynamic_module_type_cluster_lb_envoy_ptr lb_envoy_ptr, uint32_t priority, size_t index,
    envoy_dynamic_module_type_module_buffer filter_name,
    envoy_dynamic_module_type_module_buffer key, envoy_dynamic_module_type_envoy_buffer* result);

/**
 * envoy_dynamic_module_callback_cluster_lb_get_host_metadata_number is called by the module to get
 * the number value of a host's endpoint metadata by looking up the given filter name and key.
 * If the key does not exist or the value is not a number, this returns false.
 *
 * @param lb_envoy_ptr is the pointer to the Envoy cluster load balancer.
 * @param priority is the priority level.
 * @param index is the index of the host within all hosts.
 * @param filter_name is the filter namespace to look up (e.g., ``envoy.lb``).
 * @param key is the key within the filter namespace.
 * @param result is the output for the number value.
 * @return true if the key was found and the value is a number, false otherwise.
 */
bool envoy_dynamic_module_callback_cluster_lb_get_host_metadata_number(
    envoy_dynamic_module_type_cluster_lb_envoy_ptr lb_envoy_ptr, uint32_t priority, size_t index,
    envoy_dynamic_module_type_module_buffer filter_name,
    envoy_dynamic_module_type_module_buffer key, double* result);

/**
 * envoy_dynamic_module_callback_cluster_lb_get_host_metadata_bool is called by the module to get
 * the bool value of a host's endpoint metadata by looking up the given filter name and key.
 * If the key does not exist or the value is not a bool, this returns false.
 *
 * @param lb_envoy_ptr is the pointer to the Envoy cluster load balancer.
 * @param priority is the priority level.
 * @param index is the index of the host within all hosts.
 * @param filter_name is the filter namespace to look up (e.g., ``envoy.lb``).
 * @param key is the key within the filter namespace.
 * @param result is the output for the bool value.
 * @return true if the key was found and the value is a bool, false otherwise.
 */
bool envoy_dynamic_module_callback_cluster_lb_get_host_metadata_bool(
    envoy_dynamic_module_type_cluster_lb_envoy_ptr lb_envoy_ptr, uint32_t priority, size_t index,
    envoy_dynamic_module_type_module_buffer filter_name,
    envoy_dynamic_module_type_module_buffer key, bool* result);

/**
 * envoy_dynamic_module_callback_cluster_lb_get_locality_count returns the number of locality
 * buckets for the healthy hosts at a given priority. Each bucket groups hosts that share the same
 * locality.
 *
 * @param lb_envoy_ptr is the pointer to the Envoy cluster load balancer.
 * @param priority is the priority level.
 * @return the number of locality buckets at the given priority.
 */
size_t envoy_dynamic_module_callback_cluster_lb_get_locality_count(
    envoy_dynamic_module_type_cluster_lb_envoy_ptr lb_envoy_ptr, uint32_t priority);

/**
 * envoy_dynamic_module_callback_cluster_lb_get_locality_host_count returns the number of healthy
 * hosts in a specific locality bucket at a given priority.
 *
 * @param lb_envoy_ptr is the pointer to the Envoy cluster load balancer.
 * @param priority is the priority level.
 * @param locality_index is the index of the locality bucket.
 * @return the number of hosts in the locality bucket, or 0 if the index is out of bounds.
 */
size_t envoy_dynamic_module_callback_cluster_lb_get_locality_host_count(
    envoy_dynamic_module_type_cluster_lb_envoy_ptr lb_envoy_ptr, uint32_t priority,
    size_t locality_index);

/**
 * envoy_dynamic_module_callback_cluster_lb_get_locality_host_address returns the address of a host
 * within a specific locality bucket at a given priority.
 *
 * @param lb_envoy_ptr is the pointer to the Envoy cluster load balancer.
 * @param priority is the priority level.
 * @param locality_index is the index of the locality bucket.
 * @param host_index is the index of the host within the locality bucket.
 * @param result is the output for the host address as a string.
 * @return true if the host was found, false otherwise.
 */
bool envoy_dynamic_module_callback_cluster_lb_get_locality_host_address(
    envoy_dynamic_module_type_cluster_lb_envoy_ptr lb_envoy_ptr, uint32_t priority,
    size_t locality_index, size_t host_index, envoy_dynamic_module_type_envoy_buffer* result);

/**
 * envoy_dynamic_module_callback_cluster_lb_get_locality_weight returns the weight of a locality
 * bucket at a given priority. Locality weights are used for locality-aware load balancing.
 *
 * @param lb_envoy_ptr is the pointer to the Envoy cluster load balancer.
 * @param priority is the priority level.
 * @param locality_index is the index of the locality bucket.
 * @return the weight of the locality, or 0 if the index is out of bounds or weights are not set.
 */
uint32_t envoy_dynamic_module_callback_cluster_lb_get_locality_weight(
    envoy_dynamic_module_type_cluster_lb_envoy_ptr lb_envoy_ptr, uint32_t priority,
    size_t locality_index);

/**
 * envoy_dynamic_module_callback_cluster_scheduler_new creates a new scheduler for the given
 * cluster. The scheduler allows the module to dispatch events to the main thread from any thread.
 *
 * @param cluster_envoy_ptr is the pointer to the Envoy cluster.
 * @return envoy_dynamic_module_type_cluster_scheduler_module_ptr is the pointer to the scheduler.
 * The module is responsible for deleting the scheduler via
 * envoy_dynamic_module_callback_cluster_scheduler_delete when it is no longer needed.
 */
envoy_dynamic_module_type_cluster_scheduler_module_ptr
envoy_dynamic_module_callback_cluster_scheduler_new(
    envoy_dynamic_module_type_cluster_envoy_ptr cluster_envoy_ptr);

/**
 * envoy_dynamic_module_callback_cluster_scheduler_delete deletes a scheduler previously created by
 * envoy_dynamic_module_callback_cluster_scheduler_new. After this call, the scheduler pointer is
 * no longer valid.
 *
 * @param scheduler_module_ptr is the pointer to the scheduler to delete.
 */
void envoy_dynamic_module_callback_cluster_scheduler_delete(
    envoy_dynamic_module_type_cluster_scheduler_module_ptr scheduler_module_ptr);

/**
 * envoy_dynamic_module_callback_cluster_scheduler_commit schedules an event to be dispatched on
 * the main thread. When the event is dispatched, envoy_dynamic_module_on_cluster_scheduled will be
 * called with the same event_id.
 *
 * This function is thread-safe and can be called from any thread.
 *
 * @param scheduler_module_ptr is the pointer to the scheduler.
 * @param event_id is a module-defined identifier to distinguish different scheduled events.
 */
void envoy_dynamic_module_callback_cluster_scheduler_commit(
    envoy_dynamic_module_type_cluster_scheduler_module_ptr scheduler_module_ptr, uint64_t event_id);

// =============================================================================
// Cluster Dynamic Module Callbacks - Metrics
// =============================================================================

/**
 * envoy_dynamic_module_callback_cluster_config_define_counter is called by the module during
 * initialization to create a template for generating Stats::Counters with the given name and
 * labels during the lifecycle of the module.
 *
 * @param cluster_config_envoy_ptr is the pointer to the DynamicModuleClusterConfig in which the
 * counter will be defined.
 * @param name is the name of the counter to be defined.
 * @param label_names is the labels of the counter to be defined.
 * NOTE: label names could be null if the label_names_length is 0.
 * @param label_names_length is the length of the label_names.
 * NOTE: label_names_length could be 0 if there are no labels.
 * @param counter_id_ptr where the opaque ID that represents a unique metric will be stored. This
 * can be passed to envoy_dynamic_module_callback_cluster_config_increment_counter together with
 * cluster_config_envoy_ptr.
 * @return the result of the operation.
 */
envoy_dynamic_module_type_metrics_result
envoy_dynamic_module_callback_cluster_config_define_counter(
    envoy_dynamic_module_type_cluster_config_envoy_ptr cluster_config_envoy_ptr,
    envoy_dynamic_module_type_module_buffer name,
    envoy_dynamic_module_type_module_buffer* label_names, size_t label_names_length,
    size_t* counter_id_ptr);

/**
 * envoy_dynamic_module_callback_cluster_config_increment_counter is called by the module to
 * increment a previously defined counter.
 *
 * @param cluster_config_envoy_ptr is the pointer to the DynamicModuleClusterConfig.
 * @param id is the ID of the counter previously defined using the config.
 * @param label_values is the values of the labels to be incremented.
 * NOTE: label_values could be null if the label_values_length is 0.
 * @param label_values_length is the length of the label_values.
 * NOTE: label_values_length could be 0 if there are no labels. **THE LENGTH MUST MATCH THE
 * LABEL NAMES DEFINED DURING COUNTER DEFINITION.**
 * @param value is the value to increment the counter by.
 * @return the result of the operation.
 */
envoy_dynamic_module_type_metrics_result
envoy_dynamic_module_callback_cluster_config_increment_counter(
    envoy_dynamic_module_type_cluster_config_envoy_ptr cluster_config_envoy_ptr, size_t id,
    envoy_dynamic_module_type_module_buffer* label_values, size_t label_values_length,
    uint64_t value);

/**
 * envoy_dynamic_module_callback_cluster_config_define_gauge is called by the module during
 * initialization to create a template for generating Stats::Gauges with the given name and
 * labels during the lifecycle of the module.
 *
 * @param cluster_config_envoy_ptr is the pointer to the DynamicModuleClusterConfig in which the
 * gauge will be defined.
 * @param name is the name of the gauge to be defined.
 * @param label_names is the labels of the gauge to be defined.
 * NOTE: label names could be null if the label_names_length is 0.
 * @param label_names_length is the length of the label_names.
 * NOTE: label_names_length could be 0 if there are no labels.
 * @param gauge_id_ptr where the opaque ID that represents a unique metric will be stored.
 * @return the result of the operation.
 */
envoy_dynamic_module_type_metrics_result envoy_dynamic_module_callback_cluster_config_define_gauge(
    envoy_dynamic_module_type_cluster_config_envoy_ptr cluster_config_envoy_ptr,
    envoy_dynamic_module_type_module_buffer name,
    envoy_dynamic_module_type_module_buffer* label_names, size_t label_names_length,
    size_t* gauge_id_ptr);

/**
 * envoy_dynamic_module_callback_cluster_config_set_gauge is called by the module to set the value
 * of a previously defined gauge.
 *
 * @param cluster_config_envoy_ptr is the pointer to the DynamicModuleClusterConfig.
 * @param id is the ID of the gauge previously defined using the config.
 * @param label_values is the values of the labels.
 * NOTE: label_values could be null if the label_values_length is 0.
 * @param label_values_length is the length of the label_values.
 * NOTE: label_values_length could be 0 if there are no labels. **THE LENGTH MUST MATCH THE
 * LABEL NAMES DEFINED DURING GAUGE DEFINITION.**
 * @param value is the value to set the gauge to.
 * @return the result of the operation.
 */
envoy_dynamic_module_type_metrics_result envoy_dynamic_module_callback_cluster_config_set_gauge(
    envoy_dynamic_module_type_cluster_config_envoy_ptr cluster_config_envoy_ptr, size_t id,
    envoy_dynamic_module_type_module_buffer* label_values, size_t label_values_length,
    uint64_t value);

/**
 * envoy_dynamic_module_callback_cluster_config_increment_gauge is called by the module to
 * increment a previously defined gauge.
 *
 * @param cluster_config_envoy_ptr is the pointer to the DynamicModuleClusterConfig.
 * @param id is the ID of the gauge previously defined using the config.
 * @param label_values is the values of the labels.
 * NOTE: label_values could be null if the label_values_length is 0.
 * @param label_values_length is the length of the label_values.
 * NOTE: label_values_length could be 0 if there are no labels. **THE LENGTH MUST MATCH THE
 * LABEL NAMES DEFINED DURING GAUGE DEFINITION.**
 * @param value is the value to increment the gauge by.
 * @return the result of the operation.
 */
envoy_dynamic_module_type_metrics_result
envoy_dynamic_module_callback_cluster_config_increment_gauge(
    envoy_dynamic_module_type_cluster_config_envoy_ptr cluster_config_envoy_ptr, size_t id,
    envoy_dynamic_module_type_module_buffer* label_values, size_t label_values_length,
    uint64_t value);

/**
 * envoy_dynamic_module_callback_cluster_config_decrement_gauge is called by the module to
 * decrement a previously defined gauge.
 *
 * @param cluster_config_envoy_ptr is the pointer to the DynamicModuleClusterConfig.
 * @param id is the ID of the gauge previously defined using the config.
 * @param label_values is the values of the labels.
 * NOTE: label_values could be null if the label_values_length is 0.
 * @param label_values_length is the length of the label_values.
 * NOTE: label_values_length could be 0 if there are no labels. **THE LENGTH MUST MATCH THE
 * LABEL NAMES DEFINED DURING GAUGE DEFINITION.**
 * @param value is the value to decrement the gauge by.
 * @return the result of the operation.
 */
envoy_dynamic_module_type_metrics_result
envoy_dynamic_module_callback_cluster_config_decrement_gauge(
    envoy_dynamic_module_type_cluster_config_envoy_ptr cluster_config_envoy_ptr, size_t id,
    envoy_dynamic_module_type_module_buffer* label_values, size_t label_values_length,
    uint64_t value);

/**
 * envoy_dynamic_module_callback_cluster_config_define_histogram is called by the module during
 * initialization to create a template for generating Stats::Histograms with the given name and
 * labels during the lifecycle of the module.
 *
 * @param cluster_config_envoy_ptr is the pointer to the DynamicModuleClusterConfig in which the
 * histogram will be defined.
 * @param name is the name of the histogram to be defined.
 * @param label_names is the labels of the histogram to be defined.
 * NOTE: label names could be null if the label_names_length is 0.
 * @param label_names_length is the length of the label_names.
 * NOTE: label_names_length could be 0 if there are no labels.
 * @param histogram_id_ptr where the opaque ID that represents a unique metric will be stored.
 * @return the result of the operation.
 */
envoy_dynamic_module_type_metrics_result
envoy_dynamic_module_callback_cluster_config_define_histogram(
    envoy_dynamic_module_type_cluster_config_envoy_ptr cluster_config_envoy_ptr,
    envoy_dynamic_module_type_module_buffer name,
    envoy_dynamic_module_type_module_buffer* label_names, size_t label_names_length,
    size_t* histogram_id_ptr);

/**
 * envoy_dynamic_module_callback_cluster_config_record_histogram_value is called by the module to
 * record a value for a previously defined histogram.
 *
 * @param cluster_config_envoy_ptr is the pointer to the DynamicModuleClusterConfig.
 * @param id is the ID of the histogram previously defined using the config.
 * @param label_values is the values of the labels.
 * NOTE: label_values could be null if the label_values_length is 0.
 * @param label_values_length is the length of the label_values.
 * NOTE: label_values_length could be 0 if there are no labels. **THE LENGTH MUST MATCH THE
 * LABEL NAMES DEFINED DURING HISTOGRAM DEFINITION.**
 * @param value is the value to record.
 * @return the result of the operation.
 */
envoy_dynamic_module_type_metrics_result
envoy_dynamic_module_callback_cluster_config_record_histogram_value(
    envoy_dynamic_module_type_cluster_config_envoy_ptr cluster_config_envoy_ptr, size_t id,
    envoy_dynamic_module_type_module_buffer* label_values, size_t label_values_length,
    uint64_t value);

// =============================================================================
// Cluster LB Context Callbacks
// =============================================================================
//
// These callbacks allow the cluster load balancer to access per-request context
// information during envoy_dynamic_module_on_cluster_lb_choose_host. They
// provide the same capabilities as the standalone load balancer context
// callbacks (envoy_dynamic_module_callback_lb_context_*), but operate on the
// cluster_lb_context_envoy_ptr instead.

/**
 * envoy_dynamic_module_callback_cluster_lb_context_compute_hash_key computes a hash key from the
 * request context for consistent hashing.
 *
 * @param context_envoy_ptr is the per-request load balancer context.
 * @param hash_out is the output hash value. Only valid when the function returns true.
 * @return true if a hash key was computed, false if no hash key is available or the context is
 * nullptr.
 */
bool envoy_dynamic_module_callback_cluster_lb_context_compute_hash_key(
    envoy_dynamic_module_type_cluster_lb_context_envoy_ptr context_envoy_ptr, uint64_t* hash_out);

/**
 * envoy_dynamic_module_callback_cluster_lb_context_get_downstream_headers_size returns the number
 * of downstream request headers available in the context.
 *
 * @param context_envoy_ptr is the per-request load balancer context.
 * @return the number of headers, or 0 if the context is nullptr or no headers are available.
 */
size_t envoy_dynamic_module_callback_cluster_lb_context_get_downstream_headers_size(
    envoy_dynamic_module_type_cluster_lb_context_envoy_ptr context_envoy_ptr);

/**
 * envoy_dynamic_module_callback_cluster_lb_context_get_downstream_headers retrieves all downstream
 * request headers into a pre-allocated array.
 *
 * @param context_envoy_ptr is the per-request load balancer context.
 * @param result_headers is the output array of header key-value pairs. Must be pre-allocated with
 * at least the number of headers returned by
 * envoy_dynamic_module_callback_cluster_lb_context_get_downstream_headers_size.
 * @return true if the headers were retrieved, false if the context is nullptr or no headers are
 * available.
 */
bool envoy_dynamic_module_callback_cluster_lb_context_get_downstream_headers(
    envoy_dynamic_module_type_cluster_lb_context_envoy_ptr context_envoy_ptr,
    envoy_dynamic_module_type_envoy_http_header* result_headers);

/**
 * envoy_dynamic_module_callback_cluster_lb_context_get_downstream_header retrieves a single
 * downstream request header value by key and index.
 *
 * Since a header key can have multiple values, the ``index`` parameter selects a specific value.
 * The ``optional_size`` parameter can be used to retrieve the total number of values for the key.
 *
 * @param context_envoy_ptr is the per-request load balancer context.
 * @param key is the header key to look up. Owned by the module.
 * @param result_buffer is the output buffer for the header value. Owned by Envoy and valid only
 * during the current callback.
 * @param index is the index of the header value (for multi-valued headers).
 * @param optional_size is an optional output for the total number of values for this key. Can be
 * nullptr if not needed.
 * @return true if the header value was found at the given index, false otherwise.
 */
bool envoy_dynamic_module_callback_cluster_lb_context_get_downstream_header(
    envoy_dynamic_module_type_cluster_lb_context_envoy_ptr context_envoy_ptr,
    envoy_dynamic_module_type_module_buffer key,
    envoy_dynamic_module_type_envoy_buffer* result_buffer, size_t index, size_t* optional_size);

/**
 * envoy_dynamic_module_callback_cluster_lb_context_get_host_selection_retry_count returns the
 * maximum number of times host selection should be retried if the chosen host is rejected by
 * envoy_dynamic_module_callback_cluster_lb_context_should_select_another_host.
 *
 * @param context_envoy_ptr is the per-request load balancer context.
 * @return the retry count, or 0 if the context is nullptr.
 */
uint32_t envoy_dynamic_module_callback_cluster_lb_context_get_host_selection_retry_count(
    envoy_dynamic_module_type_cluster_lb_context_envoy_ptr context_envoy_ptr);

/**
 * envoy_dynamic_module_callback_cluster_lb_context_should_select_another_host checks whether the
 * load balancer should reject the given host and retry selection. This is used during retries to
 * avoid selecting hosts that were already attempted.
 *
 * @param lb_envoy_ptr is the pointer to the Envoy load balancer, used to access the host set.
 * @param context_envoy_ptr is the per-request load balancer context.
 * @param priority is the priority level of the host.
 * @param index is the index of the host within the healthy host list at the given priority.
 * @return true if another host should be selected, false otherwise.
 */
bool envoy_dynamic_module_callback_cluster_lb_context_should_select_another_host(
    envoy_dynamic_module_type_cluster_lb_envoy_ptr lb_envoy_ptr,
    envoy_dynamic_module_type_cluster_lb_context_envoy_ptr context_envoy_ptr, uint32_t priority,
    size_t index);

/**
 * envoy_dynamic_module_callback_cluster_lb_context_get_override_host returns the override host
 * address and strict mode flag from the context. Override host allows upstream filters to direct
 * the load balancer to prefer a specific host by address.
 *
 * @param context_envoy_ptr is the per-request load balancer context.
 * @param address is the output buffer for the override host address. Owned by Envoy and valid only
 * during the current callback.
 * @param strict is the output flag. When true, the load balancer should return no host if the
 * override host is not valid.
 * @return true if an override host is set, false otherwise.
 */
bool envoy_dynamic_module_callback_cluster_lb_context_get_override_host(
    envoy_dynamic_module_type_cluster_lb_context_envoy_ptr context_envoy_ptr,
    envoy_dynamic_module_type_envoy_buffer* address, bool* strict);

/**
 * envoy_dynamic_module_callback_cluster_lb_context_get_downstream_connection_sni returns the
 * requested server name (SNI) from the downstream connection associated with the request.
 *
 * @param context_envoy_ptr is the per-request load balancer context.
 * @param result_buffer is the output buffer for the SNI string. Owned by Envoy and valid only
 * during the current callback.
 * @return true if the SNI is available and non-empty, false otherwise.
 */
bool envoy_dynamic_module_callback_cluster_lb_context_get_downstream_connection_sni(
    envoy_dynamic_module_type_cluster_lb_context_envoy_ptr context_envoy_ptr,
    envoy_dynamic_module_type_envoy_buffer* result_buffer);

/**
 * envoy_dynamic_module_callback_cluster_lb_async_host_selection_complete is called by the module
 * to deliver the result of an asynchronous host selection. This must be called exactly once for
 * each async handle returned from envoy_dynamic_module_on_cluster_lb_choose_host, unless
 * envoy_dynamic_module_on_cluster_lb_cancel_host_selection was called first.
 *
 * After calling this, the module must not use the ``lb_envoy_ptr`` for this async operation again.
 *
 * @param lb_envoy_ptr is the pointer to the Envoy-side load balancer. The module receives this as
 * the second parameter to envoy_dynamic_module_on_cluster_lb_new.
 * @param context_envoy_ptr is the per-request load balancer context that was passed to the
 * original envoy_dynamic_module_on_cluster_lb_choose_host call.
 * @param host is the selected host, or nullptr if host selection failed.
 * @param details is a description of the resolution outcome (e.g., error reason). Can be empty.
 */
void envoy_dynamic_module_callback_cluster_lb_async_host_selection_complete(
    envoy_dynamic_module_type_cluster_lb_envoy_ptr lb_envoy_ptr,
    envoy_dynamic_module_type_cluster_lb_context_envoy_ptr context_envoy_ptr,
    envoy_dynamic_module_type_cluster_host_envoy_ptr host,
    envoy_dynamic_module_type_module_buffer details);

/**
 * envoy_dynamic_module_callback_cluster_http_callout sends an HTTP request to the specified cluster
 * and asynchronously delivers the response via envoy_dynamic_module_on_cluster_http_callout_done.
 *
 * This must be called on the main thread. The request requires ``:method``, ``:path``, and
 * ``host`` headers to be present.
 *
 * @param cluster_envoy_ptr is the pointer to the Envoy cluster object.
 * @param callout_id_out is a pointer to a variable where the callout ID will be stored on success.
 * @param cluster_name is the name of the target cluster to which the HTTP request is sent.
 * @param headers is the array of request headers. Must include ``:method``, ``:path``, and
 * ``host``.
 * @param headers_size is the number of request headers.
 * @param body is the request body. Can be empty (length 0) for requests without a body.
 * @param timeout_milliseconds is the timeout for the request in milliseconds.
 * @return the result of the callout initialization.
 */
envoy_dynamic_module_type_http_callout_init_result
envoy_dynamic_module_callback_cluster_http_callout(
    envoy_dynamic_module_type_cluster_envoy_ptr cluster_envoy_ptr, uint64_t* callout_id_out,
    envoy_dynamic_module_type_module_buffer cluster_name,
    envoy_dynamic_module_type_module_http_header* headers, size_t headers_size,
    envoy_dynamic_module_type_module_buffer body, uint64_t timeout_milliseconds);

// =============================================================================
// Cluster LB Host Membership Update
// =============================================================================

/**
 * envoy_dynamic_module_on_cluster_lb_on_host_membership_update is called on each worker thread
 * when the set of hosts in the cluster changes. This is triggered by
 * envoy_dynamic_module_callback_cluster_add_hosts,
 * envoy_dynamic_module_callback_cluster_remove_hosts, or any other mechanism that modifies
 * the cluster's host set.
 *
 * During this callback, the module can call
 * envoy_dynamic_module_callback_cluster_lb_get_member_update_host_address to get the addresses of
 * the added or removed hosts by index.
 *
 * After this callback returns, the module can use the standard host query callbacks to inspect the
 * new host state.
 *
 * This is optional. Only modules whose per-worker load balancers need to rebuild internal data
 * structures (e.g., hash rings, address-to-index maps) when the host set changes need to implement
 * this.
 *
 * @param lb_envoy_ptr is the pointer to the Envoy cluster load balancer.
 * @param lb_module_ptr is the pointer to the in-module load balancer instance.
 * @param num_hosts_added is the number of hosts added.
 * @param num_hosts_removed is the number of hosts removed.
 */
void envoy_dynamic_module_on_cluster_lb_on_host_membership_update(
    envoy_dynamic_module_type_cluster_lb_envoy_ptr lb_envoy_ptr,
    envoy_dynamic_module_type_cluster_lb_module_ptr lb_module_ptr, size_t num_hosts_added,
    size_t num_hosts_removed);

/**
 * envoy_dynamic_module_callback_cluster_lb_get_member_update_host_address returns the address of
 * an added or removed host during the on_cluster_lb_on_host_membership_update event hook. This
 * callback is only valid during envoy_dynamic_module_on_cluster_lb_on_host_membership_update.
 *
 * @param lb_envoy_ptr is the pointer to the Envoy cluster load balancer.
 * @param index is the index of the host in the added or removed list.
 * @param is_added is true to get an added host address, false to get a removed host address.
 * @param result is the output buffer for the host address string. The buffer points to Envoy-owned
 * memory that is valid only for the duration of the on_cluster_lb_on_host_membership_update
 * callback.
 * @return true if the host was found, false otherwise.
 */
bool envoy_dynamic_module_callback_cluster_lb_get_member_update_host_address(
    envoy_dynamic_module_type_cluster_lb_envoy_ptr lb_envoy_ptr, size_t index, bool is_added,
    envoy_dynamic_module_type_envoy_buffer* result);

// =============================================================================
// =============================== Load Balancer ===============================
// =============================================================================
//
// This extension enables custom load balancing algorithms via dynamic modules.
// The module implements the host selection logic while Envoy handles cluster
// management, health checking, and connection pooling.
//
// The module only needs to implement the chooseHost event hook which receives
// host information and context to make a selection decision.

// =============================================================================
// Load Balancer Types
// =============================================================================

/**
 * envoy_dynamic_module_type_lb_config_envoy_ptr is a raw pointer to the
 * DynamicModuleLbConfig class in Envoy. This is passed to the module when
 * creating a new in-module load balancer configuration.
 *
 * OWNERSHIP: Envoy owns the pointer.
 */
typedef void* envoy_dynamic_module_type_lb_config_envoy_ptr;

/**
 * envoy_dynamic_module_type_lb_config_module_ptr is a pointer to an in-module
 * load balancer configuration. This is created by the module when the
 * configuration is loaded.
 *
 * OWNERSHIP: The module is responsible for managing the lifetime of the pointer. The pointer can
 * be released when envoy_dynamic_module_on_lb_config_destroy is called.
 */
typedef const void* envoy_dynamic_module_type_lb_config_module_ptr;

/**
 * envoy_dynamic_module_type_lb_envoy_ptr is a raw pointer to the
 * DynamicModuleLoadBalancer class in Envoy. This is passed to the module for callbacks.
 *
 * OWNERSHIP: Envoy owns the pointer. The pointer is valid only during the event hook calls.
 */
typedef void* envoy_dynamic_module_type_lb_envoy_ptr;

/**
 * envoy_dynamic_module_type_lb_module_ptr is a pointer to an in-module
 * load balancer instance. This is created per worker thread.
 *
 * OWNERSHIP: The module is responsible for managing the lifetime of the pointer. The pointer can
 * be released when envoy_dynamic_module_on_lb_destroy is called.
 */
typedef const void* envoy_dynamic_module_type_lb_module_ptr;

/**
 * envoy_dynamic_module_type_lb_context_envoy_ptr is a raw pointer to the
 * LoadBalancerContext in Envoy. This is passed to the module during chooseHost.
 *
 * OWNERSHIP: Envoy owns the pointer. The pointer is valid only during the chooseHost call.
 */
typedef void* envoy_dynamic_module_type_lb_context_envoy_ptr;

// =============================================================================
// Load Balancer Event Hooks
// =============================================================================

/**
 * envoy_dynamic_module_on_lb_config_new is called by the main thread when
 * a new load balancer configuration is loaded.
 *
 * @param lb_config_envoy_ptr is the pointer to the Envoy load balancer config object.
 * @param name is the name identifying the load balancer implementation in the module.
 * @param config is the configuration bytes for the module.
 * @return envoy_dynamic_module_type_lb_config_module_ptr is the pointer to
 * the in-module configuration. Returning nullptr indicates a failure and the configuration will
 * be rejected.
 */
envoy_dynamic_module_type_lb_config_module_ptr envoy_dynamic_module_on_lb_config_new(
    envoy_dynamic_module_type_lb_config_envoy_ptr lb_config_envoy_ptr,
    envoy_dynamic_module_type_envoy_buffer name, envoy_dynamic_module_type_envoy_buffer config);

/**
 * envoy_dynamic_module_on_lb_config_destroy is called when the load balancer
 * configuration is destroyed. The module should release any resources associated with it.
 *
 * @param config_module_ptr is the pointer to the in-module configuration.
 */
void envoy_dynamic_module_on_lb_config_destroy(
    envoy_dynamic_module_type_lb_config_module_ptr config_module_ptr);

/**
 * envoy_dynamic_module_on_lb_new is called when a new load balancer instance is
 * created for a worker thread.
 *
 * @param config_module_ptr is the pointer to the in-module configuration.
 * @param lb_envoy_ptr is the pointer to the Envoy load balancer object for callbacks.
 * @return envoy_dynamic_module_type_lb_module_ptr is the pointer to the in-module
 * load balancer instance. Returning nullptr indicates a failure.
 */
envoy_dynamic_module_type_lb_module_ptr
envoy_dynamic_module_on_lb_new(envoy_dynamic_module_type_lb_config_module_ptr config_module_ptr,
                               envoy_dynamic_module_type_lb_envoy_ptr lb_envoy_ptr);

/**
 * envoy_dynamic_module_on_lb_choose_host is called when a host needs to be selected
 * for an upstream request. The module should select a host by writing the priority level
 * and host index (within the healthy hosts at that priority) into the output parameters.
 *
 * @param lb_envoy_ptr is the pointer to the Envoy load balancer object.
 * @param lb_module_ptr is the pointer to the in-module load balancer instance.
 * @param context_envoy_ptr is the pointer to the LoadBalancerContext (may be null).
 * @param result_priority is the output parameter for the priority level of the selected host.
 * @param result_index is the output parameter for the index of the selected host within the
 * healthy hosts at the given priority.
 * @return true if a host was selected (result_priority and result_index are populated),
 * false if no host should be selected (which will result in no upstream connection).
 */
bool envoy_dynamic_module_on_lb_choose_host(
    envoy_dynamic_module_type_lb_envoy_ptr lb_envoy_ptr,
    envoy_dynamic_module_type_lb_module_ptr lb_module_ptr,
    envoy_dynamic_module_type_lb_context_envoy_ptr context_envoy_ptr, uint32_t* result_priority,
    uint32_t* result_index);

/**
 * envoy_dynamic_module_on_lb_on_host_membership_update is called when the set of hosts in the
 * cluster changes. This is triggered by EDS updates, health check transitions, or any other
 * mechanism that adds or removes hosts from the priority set.
 *
 * During this callback, the module can call
 * envoy_dynamic_module_callback_lb_get_member_update_host_address to get the addresses of the
 * added or removed hosts by index.
 *
 * After this callback returns, the module can use the standard host query callbacks
 * (get_hosts_count, get_healthy_hosts_count, etc.) to inspect the new host state.
 *
 * @param lb_envoy_ptr is the pointer to the Envoy load balancer object.
 * @param lb_module_ptr is the pointer to the in-module load balancer instance.
 * @param num_hosts_added is the number of hosts added.
 * @param num_hosts_removed is the number of hosts removed.
 */
void envoy_dynamic_module_on_lb_on_host_membership_update(
    envoy_dynamic_module_type_lb_envoy_ptr lb_envoy_ptr,
    envoy_dynamic_module_type_lb_module_ptr lb_module_ptr, size_t num_hosts_added,
    size_t num_hosts_removed);

/**
 * envoy_dynamic_module_on_lb_destroy is called when the load balancer instance is
 * destroyed. The module should release any resources associated with it.
 *
 * @param lb_module_ptr is the pointer to the in-module load balancer instance.
 */
void envoy_dynamic_module_on_lb_destroy(envoy_dynamic_module_type_lb_module_ptr lb_module_ptr);

// =============================================================================
// Load Balancer Callbacks
// =============================================================================

/**
 * envoy_dynamic_module_callback_lb_get_cluster_name returns the name of the cluster.
 *
 * @param lb_envoy_ptr is the pointer to the Envoy load balancer object.
 * @param result is the output for the cluster name.
 */
void envoy_dynamic_module_callback_lb_get_cluster_name(
    envoy_dynamic_module_type_lb_envoy_ptr lb_envoy_ptr,
    envoy_dynamic_module_type_envoy_buffer* result);

/**
 * envoy_dynamic_module_callback_lb_get_hosts_count returns the number of all hosts
 * in the cluster (across all priorities and health states).
 *
 * @param lb_envoy_ptr is the pointer to the Envoy load balancer object.
 * @param priority is the priority level to query.
 * @return the number of all hosts at the given priority.
 */
size_t envoy_dynamic_module_callback_lb_get_hosts_count(
    envoy_dynamic_module_type_lb_envoy_ptr lb_envoy_ptr, uint32_t priority);

/**
 * envoy_dynamic_module_callback_lb_get_healthy_hosts_count returns the number of healthy hosts
 * in the cluster at a given priority.
 *
 * @param lb_envoy_ptr is the pointer to the Envoy load balancer object.
 * @param priority is the priority level to query.
 * @return the number of healthy hosts at the given priority.
 */
size_t envoy_dynamic_module_callback_lb_get_healthy_hosts_count(
    envoy_dynamic_module_type_lb_envoy_ptr lb_envoy_ptr, uint32_t priority);

/**
 * envoy_dynamic_module_callback_lb_get_degraded_hosts_count returns the number of degraded hosts
 * in the cluster at a given priority.
 *
 * @param lb_envoy_ptr is the pointer to the Envoy load balancer object.
 * @param priority is the priority level to query.
 * @return the number of degraded hosts at the given priority.
 */
size_t envoy_dynamic_module_callback_lb_get_degraded_hosts_count(
    envoy_dynamic_module_type_lb_envoy_ptr lb_envoy_ptr, uint32_t priority);

/**
 * envoy_dynamic_module_callback_lb_get_priority_set_size returns the number of priority levels
 * in the cluster.
 *
 * @param lb_envoy_ptr is the pointer to the Envoy load balancer object.
 * @return the number of priority levels.
 */
size_t envoy_dynamic_module_callback_lb_get_priority_set_size(
    envoy_dynamic_module_type_lb_envoy_ptr lb_envoy_ptr);

/**
 * envoy_dynamic_module_callback_lb_get_healthy_host_address returns the address of a host
 * by index within the healthy hosts at a given priority.
 *
 * @param lb_envoy_ptr is the pointer to the Envoy load balancer object.
 * @param priority is the priority level.
 * @param index is the index of the host within healthy hosts.
 * @param result is the output for the host address as a string.
 * @return true if the host was found, false otherwise.
 */
bool envoy_dynamic_module_callback_lb_get_healthy_host_address(
    envoy_dynamic_module_type_lb_envoy_ptr lb_envoy_ptr, uint32_t priority, size_t index,
    envoy_dynamic_module_type_envoy_buffer* result);

/**
 * envoy_dynamic_module_callback_lb_get_healthy_host_weight returns the load balancing weight
 * of a host by index within the healthy hosts at a given priority.
 *
 * @param lb_envoy_ptr is the pointer to the Envoy load balancer object.
 * @param priority is the priority level.
 * @param index is the index of the host within healthy hosts.
 * @return the weight of the host (1-128), or 0 if the host was not found.
 */
uint32_t envoy_dynamic_module_callback_lb_get_healthy_host_weight(
    envoy_dynamic_module_type_lb_envoy_ptr lb_envoy_ptr, uint32_t priority, size_t index);

/**
 * envoy_dynamic_module_callback_lb_get_host_health returns the health status of a host
 * by index within all hosts at a given priority.
 *
 * @param lb_envoy_ptr is the pointer to the Envoy load balancer object.
 * @param priority is the priority level.
 * @param index is the index of the host within all hosts.
 * @return the health status of the host.
 */
envoy_dynamic_module_type_host_health envoy_dynamic_module_callback_lb_get_host_health(
    envoy_dynamic_module_type_lb_envoy_ptr lb_envoy_ptr, uint32_t priority, size_t index);

/**
 * envoy_dynamic_module_callback_lb_get_host_health_by_address looks up a host by its address
 * string across all priorities and returns the health status. This uses the cross-priority host
 * map internally, providing O(1) lookup by address instead of requiring the caller to iterate
 * through all hosts by index.
 *
 * The address string must match the format returned by host->address()->asStringView(), which is
 * typically "ip:port" (e.g., "10.0.0.1:8080").
 *
 * @param lb_envoy_ptr is the pointer to the Envoy load balancer object.
 * @param address is the address string to look up.
 * @param result is the output for the health status of the host.
 * @return true if the host was found, false if the address is not in the host map.
 */
bool envoy_dynamic_module_callback_lb_get_host_health_by_address(
    envoy_dynamic_module_type_lb_envoy_ptr lb_envoy_ptr,
    envoy_dynamic_module_type_module_buffer address, envoy_dynamic_module_type_host_health* result);

/**
 * envoy_dynamic_module_callback_lb_get_host_address returns the address of a host
 * by index within all hosts at a given priority.
 *
 * @param lb_envoy_ptr is the pointer to the Envoy load balancer object.
 * @param priority is the priority level.
 * @param index is the index of the host within all hosts.
 * @param result is the output for the host address as a string.
 * @return true if the host was found, false otherwise.
 */
bool envoy_dynamic_module_callback_lb_get_host_address(
    envoy_dynamic_module_type_lb_envoy_ptr lb_envoy_ptr, uint32_t priority, size_t index,
    envoy_dynamic_module_type_envoy_buffer* result);

/**
 * envoy_dynamic_module_callback_lb_get_host_weight returns the load balancing weight
 * of a host by index within all hosts at a given priority.
 *
 * @param lb_envoy_ptr is the pointer to the Envoy load balancer object.
 * @param priority is the priority level.
 * @param index is the index of the host within all hosts.
 * @return the weight of the host (1-128), or 0 if the host was not found.
 */
uint32_t envoy_dynamic_module_callback_lb_get_host_weight(
    envoy_dynamic_module_type_lb_envoy_ptr lb_envoy_ptr, uint32_t priority, size_t index);

/**
 * envoy_dynamic_module_callback_lb_get_host_locality returns the locality information
 * (region, zone, sub_zone) for a host by index within all hosts at a given priority.
 * This enables zone-aware and locality-aware load balancing algorithms.
 *
 * @param lb_envoy_ptr is the pointer to the Envoy load balancer object.
 * @param priority is the priority level.
 * @param index is the index of the host within all hosts.
 * @param region is the output for the region string. Can be null if not needed.
 * @param zone is the output for the zone string. Can be null if not needed.
 * @param sub_zone is the output for the sub-zone string. Can be null if not needed.
 * @return true if the host was found, false otherwise.
 */
bool envoy_dynamic_module_callback_lb_get_host_locality(
    envoy_dynamic_module_type_lb_envoy_ptr lb_envoy_ptr, uint32_t priority, size_t index,
    envoy_dynamic_module_type_envoy_buffer* region, envoy_dynamic_module_type_envoy_buffer* zone,
    envoy_dynamic_module_type_envoy_buffer* sub_zone);

/**
 * envoy_dynamic_module_callback_lb_set_host_data stores a module-defined opaque value on a host
 * identified by priority and index within all hosts. This data is stored per load balancer instance
 * (i.e., per worker thread) and can be used to attach per-host state for load balancing decisions
 * such as moving averages or request tracking.
 *
 * The data is only valid for the lifetime of the load balancer instance. It is not shared across
 * worker threads. Callers are responsible for managing the memory pointed to by the stored value
 * if it represents a pointer.
 *
 * @param lb_envoy_ptr is the pointer to the Envoy load balancer object.
 * @param priority is the priority level.
 * @param index is the index of the host within all hosts.
 * @param data is the opaque value to store. Use 0 to clear the data.
 * @return true if the data was stored successfully, false if the host was not found.
 */
bool envoy_dynamic_module_callback_lb_set_host_data(
    envoy_dynamic_module_type_lb_envoy_ptr lb_envoy_ptr, uint32_t priority, size_t index,
    uintptr_t data);

/**
 * envoy_dynamic_module_callback_lb_get_host_data retrieves a module-defined opaque value
 * previously stored on a host via envoy_dynamic_module_callback_lb_set_host_data.
 *
 * @param lb_envoy_ptr is the pointer to the Envoy load balancer object.
 * @param priority is the priority level.
 * @param index is the index of the host within all hosts.
 * @param data is the output for the stored opaque value. Set to 0 if no data was stored.
 * @return true if the host was found, false otherwise.
 */
bool envoy_dynamic_module_callback_lb_get_host_data(
    envoy_dynamic_module_type_lb_envoy_ptr lb_envoy_ptr, uint32_t priority, size_t index,
    uintptr_t* data);

/**
 * envoy_dynamic_module_callback_lb_get_host_metadata_string is called by the module to get
 * the string value of a host's endpoint metadata by looking up the given filter name and key.
 * If the key does not exist or the value is not a string, this returns false.
 *
 * @param lb_envoy_ptr is the pointer to the Envoy load balancer object.
 * @param priority is the priority level.
 * @param index is the index of the host within all hosts.
 * @param filter_name is the filter namespace to look up (e.g., "envoy.lb").
 * @param key is the key within the filter namespace.
 * @param result is the output for the string value. The buffer is owned by Envoy and is valid
 * until the end of the current event hook.
 * @return true if the key was found and the value is a string, false otherwise.
 */
bool envoy_dynamic_module_callback_lb_get_host_metadata_string(
    envoy_dynamic_module_type_lb_envoy_ptr lb_envoy_ptr, uint32_t priority, size_t index,
    envoy_dynamic_module_type_module_buffer filter_name,
    envoy_dynamic_module_type_module_buffer key, envoy_dynamic_module_type_envoy_buffer* result);

/**
 * envoy_dynamic_module_callback_lb_get_host_metadata_number is called by the module to get
 * the number value of a host's endpoint metadata by looking up the given filter name and key.
 * If the key does not exist or the value is not a number, this returns false.
 *
 * @param lb_envoy_ptr is the pointer to the Envoy load balancer object.
 * @param priority is the priority level.
 * @param index is the index of the host within all hosts.
 * @param filter_name is the filter namespace to look up (e.g., "envoy.lb").
 * @param key is the key within the filter namespace.
 * @param result is the output for the number value.
 * @return true if the key was found and the value is a number, false otherwise.
 */
bool envoy_dynamic_module_callback_lb_get_host_metadata_number(
    envoy_dynamic_module_type_lb_envoy_ptr lb_envoy_ptr, uint32_t priority, size_t index,
    envoy_dynamic_module_type_module_buffer filter_name,
    envoy_dynamic_module_type_module_buffer key, double* result);

/**
 * envoy_dynamic_module_callback_lb_get_host_metadata_bool is called by the module to get
 * the bool value of a host's endpoint metadata by looking up the given filter name and key.
 * If the key does not exist or the value is not a bool, this returns false.
 *
 * @param lb_envoy_ptr is the pointer to the Envoy load balancer object.
 * @param priority is the priority level.
 * @param index is the index of the host within all hosts.
 * @param filter_name is the filter namespace to look up (e.g., "envoy.lb").
 * @param key is the key within the filter namespace.
 * @param result is the output for the bool value.
 * @return true if the key was found and the value is a bool, false otherwise.
 */
bool envoy_dynamic_module_callback_lb_get_host_metadata_bool(
    envoy_dynamic_module_type_lb_envoy_ptr lb_envoy_ptr, uint32_t priority, size_t index,
    envoy_dynamic_module_type_module_buffer filter_name,
    envoy_dynamic_module_type_module_buffer key, bool* result);

/**
 * envoy_dynamic_module_callback_lb_get_locality_count returns the number of locality buckets
 * for the healthy hosts at a given priority. Each bucket groups hosts that share the same locality.
 *
 * @param lb_envoy_ptr is the pointer to the Envoy load balancer object.
 * @param priority is the priority level.
 * @return the number of locality buckets at the given priority.
 */
size_t envoy_dynamic_module_callback_lb_get_locality_count(
    envoy_dynamic_module_type_lb_envoy_ptr lb_envoy_ptr, uint32_t priority);

/**
 * envoy_dynamic_module_callback_lb_get_locality_host_count returns the number of healthy hosts
 * in a specific locality bucket at a given priority.
 *
 * @param lb_envoy_ptr is the pointer to the Envoy load balancer object.
 * @param priority is the priority level.
 * @param locality_index is the index of the locality bucket.
 * @return the number of hosts in the locality bucket, or 0 if the index is out of bounds.
 */
size_t envoy_dynamic_module_callback_lb_get_locality_host_count(
    envoy_dynamic_module_type_lb_envoy_ptr lb_envoy_ptr, uint32_t priority, size_t locality_index);

/**
 * envoy_dynamic_module_callback_lb_get_locality_host_address returns the address of a host
 * within a specific locality bucket at a given priority.
 *
 * @param lb_envoy_ptr is the pointer to the Envoy load balancer object.
 * @param priority is the priority level.
 * @param locality_index is the index of the locality bucket.
 * @param host_index is the index of the host within the locality bucket.
 * @param result is the output for the host address as a string.
 * @return true if the host was found, false otherwise.
 */
bool envoy_dynamic_module_callback_lb_get_locality_host_address(
    envoy_dynamic_module_type_lb_envoy_ptr lb_envoy_ptr, uint32_t priority, size_t locality_index,
    size_t host_index, envoy_dynamic_module_type_envoy_buffer* result);

/**
 * envoy_dynamic_module_callback_lb_get_locality_weight returns the weight of a locality bucket
 * at a given priority. Locality weights are used for locality-aware load balancing.
 *
 * @param lb_envoy_ptr is the pointer to the Envoy load balancer object.
 * @param priority is the priority level.
 * @param locality_index is the index of the locality bucket.
 * @return the weight of the locality, or 0 if the index is out of bounds or weights are not set.
 */
uint32_t envoy_dynamic_module_callback_lb_get_locality_weight(
    envoy_dynamic_module_type_lb_envoy_ptr lb_envoy_ptr, uint32_t priority, size_t locality_index);

/**
 * envoy_dynamic_module_callback_lb_context_compute_hash_key computes a hash key from
 * the load balancer context.
 *
 * @param context_envoy_ptr is the pointer to the LoadBalancerContext.
 * @param hash_out is the output for the computed hash.
 * @return true if a hash was computed, false if the context is null or no hash is available.
 */
bool envoy_dynamic_module_callback_lb_context_compute_hash_key(
    envoy_dynamic_module_type_lb_context_envoy_ptr context_envoy_ptr, uint64_t* hash_out);

/**
 * envoy_dynamic_module_callback_lb_context_get_downstream_headers_size returns
 * the number of downstream request headers. Combined with
 * envoy_dynamic_module_callback_lb_context_get_downstream_headers, this can be used to iterate
 * over all downstream request headers.
 *
 * @param context_envoy_ptr is the pointer to the LoadBalancerContext.
 * @return the number of headers, or 0 if context is null or headers are not available.
 */
size_t envoy_dynamic_module_callback_lb_context_get_downstream_headers_size(
    envoy_dynamic_module_type_lb_context_envoy_ptr context_envoy_ptr);

/**
 * envoy_dynamic_module_callback_lb_context_get_downstream_headers is called by the module to get
 * all the downstream request headers. The headers are returned as an array of
 * envoy_dynamic_module_type_envoy_http_header.
 *
 * PRECONDITION: The module must ensure that the result_headers is valid and has enough length to
 * store all the headers. The module can use
 * envoy_dynamic_module_callback_lb_context_get_downstream_headers_size to get the number of
 * headers before calling this function.
 *
 * @param context_envoy_ptr is the pointer to the LoadBalancerContext.
 * @param result_headers is the pointer to the array of envoy_dynamic_module_type_envoy_http_header
 * where the headers will be stored. The lifetime of the buffer of key and value of each header is
 * guaranteed until the end of the current choose_host callback.
 * @return true if the operation is successful, false otherwise.
 */
bool envoy_dynamic_module_callback_lb_context_get_downstream_headers(
    envoy_dynamic_module_type_lb_context_envoy_ptr context_envoy_ptr,
    envoy_dynamic_module_type_envoy_http_header* result_headers);

/**
 * envoy_dynamic_module_callback_lb_context_get_downstream_header is called by the module to get
 * the value of the downstream request header with the given key. Since a header can have multiple
 * values, the index is used to get the specific value. This returns the number of values for the
 * given key via optional_size, so it can be used to iterate over all values by starting from 0 and
 * incrementing the index until the return value.
 *
 * @param context_envoy_ptr is the pointer to the LoadBalancerContext.
 * @param key is the key of the header.
 * @param result_buffer is the buffer where the value will be stored. If the key does not exist or
 * the index is out of range, this will be set to a null buffer (length 0).
 * @param index is the index of the header value in the list of values for the given key.
 * @param optional_size is the pointer to the variable where the number of values for the given key
 * will be stored.
 * NOTE: This parameter is optional and can be null if the module does not need this information.
 * @return true if the operation is successful, false otherwise.
 */
bool envoy_dynamic_module_callback_lb_context_get_downstream_header(
    envoy_dynamic_module_type_lb_context_envoy_ptr context_envoy_ptr,
    envoy_dynamic_module_type_module_buffer key,
    envoy_dynamic_module_type_envoy_buffer* result_buffer, size_t index, size_t* optional_size);

/**
 * envoy_dynamic_module_callback_lb_context_get_host_selection_retry_count returns the number
 * of times host selection should be retried if the chosen host is rejected by
 * shouldSelectAnotherHost. Built-in load balancers use this value as the upper bound of a
 * retry loop during host selection.
 *
 * @param context_envoy_ptr is the pointer to the LoadBalancerContext.
 * @return the maximum number of host selection retries, or 0 if the context is null.
 */
uint32_t envoy_dynamic_module_callback_lb_context_get_host_selection_retry_count(
    envoy_dynamic_module_type_lb_context_envoy_ptr context_envoy_ptr);

/**
 * envoy_dynamic_module_callback_lb_context_should_select_another_host checks whether the
 * load balancer should reject the given host and retry selection. This is used during retries
 * to avoid selecting hosts that were already attempted. The host is identified by priority
 * and index within all hosts at that priority.
 *
 * @param lb_envoy_ptr is the pointer to the Envoy load balancer object.
 * @param context_envoy_ptr is the pointer to the LoadBalancerContext.
 * @param priority is the priority level.
 * @param index is the index of the host within all hosts.
 * @return true if the host should be rejected and selection retried, false otherwise.
 */
bool envoy_dynamic_module_callback_lb_context_should_select_another_host(
    envoy_dynamic_module_type_lb_envoy_ptr lb_envoy_ptr,
    envoy_dynamic_module_type_lb_context_envoy_ptr context_envoy_ptr, uint32_t priority,
    size_t index);

/**
 * envoy_dynamic_module_callback_lb_context_get_override_host returns the override host address
 * and strict mode flag from the load balancer context. Override host allows upstream filters to
 * direct the load balancer to prefer a specific host by address. Note that override host
 * resolution is normally handled by the ClusterManager before the load balancer is invoked, so
 * this callback provides read-only access to the override host preference.
 *
 * @param context_envoy_ptr is the pointer to the LoadBalancerContext.
 * @param address is the output buffer for the override host address string. The buffer points to
 * Envoy-owned memory valid for the duration of the context.
 * @param strict is the output for the strict mode flag. When true, the load balancer should
 * return nullptr if the override host is not valid. When false, the load balancer should fall
 * back to normal selection.
 * @return true if an override host is set, false otherwise.
 */
bool envoy_dynamic_module_callback_lb_context_get_override_host(
    envoy_dynamic_module_type_lb_context_envoy_ptr context_envoy_ptr,
    envoy_dynamic_module_type_envoy_buffer* address, bool* strict);

/**
 * envoy_dynamic_module_callback_lb_get_member_update_host_address returns the address of an added
 * or removed host during the on_host_membership_update event hook. This callback is only valid
 * during envoy_dynamic_module_on_lb_on_host_membership_update.
 *
 * @param lb_envoy_ptr is the pointer to the Envoy load balancer object.
 * @param index is the index of the host in the added or removed list.
 * @param is_added is true to get an added host address, false to get a removed host address.
 * @param result is the output buffer for the host address string. The buffer points to Envoy-owned
 * memory that is valid only for the duration of the on_host_membership_update callback.
 * @return true if the host was found, false otherwise.
 */
bool envoy_dynamic_module_callback_lb_get_member_update_host_address(
    envoy_dynamic_module_type_lb_envoy_ptr lb_envoy_ptr, size_t index, bool is_added,
    envoy_dynamic_module_type_envoy_buffer* result);

/**
 * envoy_dynamic_module_callback_lb_get_host_stat returns the value of a per-host stat
 * identified by the stat enum. This provides access to host-level counters and gauges such as
 * total connections, request errors, active requests, and active connections.
 *
 * @param lb_envoy_ptr is the pointer to the Envoy load balancer object.
 * @param priority is the priority level.
 * @param index is the index of the host within all hosts.
 * @param stat is the host stat to query.
 * @return the stat value, or 0 if the host was not found or the stat is invalid.
 */
uint64_t
envoy_dynamic_module_callback_lb_get_host_stat(envoy_dynamic_module_type_lb_envoy_ptr lb_envoy_ptr,
                                               uint32_t priority, size_t index,
                                               envoy_dynamic_module_type_host_stat stat);

// =============================================================================
// Load Balancer Callbacks - Metrics
// =============================================================================

/**
 * envoy_dynamic_module_callback_lb_config_define_counter is called by the module during
 * initialization to create a template for generating Stats::Counters with the given name and
 * labels during the lifecycle of the module.
 *
 * @param lb_config_envoy_ptr is the pointer to the DynamicModuleLbConfig in which the counter
 * will be defined.
 * @param name is the name of the counter to be defined.
 * @param label_names is the labels of the counter to be defined.
 * NOTE: label names could be null if the label_names_length is 0.
 * @param label_names_length is the length of the label_names.
 * NOTE: label_names_length could be 0 if there are no labels.
 * @param counter_id_ptr where the opaque ID that represents a unique metric will be stored. This
 * can be passed to envoy_dynamic_module_callback_lb_config_increment_counter together with
 * lb_config_envoy_ptr.
 * @return the result of the operation.
 */
envoy_dynamic_module_type_metrics_result envoy_dynamic_module_callback_lb_config_define_counter(
    envoy_dynamic_module_type_lb_config_envoy_ptr lb_config_envoy_ptr,
    envoy_dynamic_module_type_module_buffer name,
    envoy_dynamic_module_type_module_buffer* label_names, size_t label_names_length,
    size_t* counter_id_ptr);

/**
 * envoy_dynamic_module_callback_lb_config_increment_counter is called by the module to increment
 * a previously defined counter.
 *
 * @param lb_config_envoy_ptr is the pointer to the DynamicModuleLbConfig.
 * @param id is the ID of the counter previously defined using the config.
 * @param label_values is the values of the labels to be incremented.
 * NOTE: label_values could be null if the label_values_length is 0.
 * @param label_values_length is the length of the label_values.
 * NOTE: label_values_length could be 0 if there are no labels. **THE LENGTH MUST MATCH THE
 * LABEL NAMES DEFINED DURING COUNTER DEFINITION.**
 * @param value is the value to increment the counter by.
 * @return the result of the operation.
 */
envoy_dynamic_module_type_metrics_result envoy_dynamic_module_callback_lb_config_increment_counter(
    envoy_dynamic_module_type_lb_config_envoy_ptr lb_config_envoy_ptr, size_t id,
    envoy_dynamic_module_type_module_buffer* label_values, size_t label_values_length,
    uint64_t value);

/**
 * envoy_dynamic_module_callback_lb_config_define_gauge is called by the module during
 * initialization to create a template for generating Stats::Gauges with the given name and labels
 * during the lifecycle of the module.
 *
 * @param lb_config_envoy_ptr is the pointer to the DynamicModuleLbConfig in which the gauge
 * will be defined.
 * @param name is the name of the gauge to be defined.
 * @param label_names is the labels of the gauge to be defined.
 * NOTE: label names could be null if the label_names_length is 0.
 * @param label_names_length is the length of the label_names.
 * NOTE: label_names_length could be 0 if there are no labels.
 * @param gauge_id_ptr where the opaque ID that represents a unique metric will be stored. This can
 * be passed to envoy_dynamic_module_callback_lb_config_set_gauge together with
 * lb_config_envoy_ptr.
 * @return the result of the operation.
 */
envoy_dynamic_module_type_metrics_result envoy_dynamic_module_callback_lb_config_define_gauge(
    envoy_dynamic_module_type_lb_config_envoy_ptr lb_config_envoy_ptr,
    envoy_dynamic_module_type_module_buffer name,
    envoy_dynamic_module_type_module_buffer* label_names, size_t label_names_length,
    size_t* gauge_id_ptr);

/**
 * envoy_dynamic_module_callback_lb_config_set_gauge is called by the module to set the value of a
 * previously defined gauge.
 *
 * @param lb_config_envoy_ptr is the pointer to the DynamicModuleLbConfig.
 * @param id is the ID of the gauge previously defined using the config.
 * @param label_values is the values of the labels to be set.
 * NOTE: label_values could be null if the label_values_length is 0.
 * @param label_values_length is the length of the label_values.
 * NOTE: label_values_length could be 0 if there are no labels. **THE LENGTH MUST MATCH THE
 * LABEL NAMES DEFINED DURING GAUGE DEFINITION.**
 * @param value is the value to set the gauge to.
 * @return the result of the operation.
 */
envoy_dynamic_module_type_metrics_result envoy_dynamic_module_callback_lb_config_set_gauge(
    envoy_dynamic_module_type_lb_config_envoy_ptr lb_config_envoy_ptr, size_t id,
    envoy_dynamic_module_type_module_buffer* label_values, size_t label_values_length,
    uint64_t value);

/**
 * envoy_dynamic_module_callback_lb_config_increment_gauge is called by the module to increase the
 * value of a previously defined gauge.
 *
 * @param lb_config_envoy_ptr is the pointer to the DynamicModuleLbConfig.
 * @param id is the ID of the gauge previously defined using the config.
 * @param label_values is the values of the labels to be increased.
 * NOTE: label_values could be null if the label_values_length is 0.
 * @param label_values_length is the length of the label_values.
 * NOTE: label_values_length could be 0 if there are no labels. **THE LENGTH MUST MATCH THE
 * LABEL NAMES DEFINED DURING GAUGE DEFINITION.**
 * @param value is the value to increase the gauge by.
 * @return the result of the operation.
 */
envoy_dynamic_module_type_metrics_result envoy_dynamic_module_callback_lb_config_increment_gauge(
    envoy_dynamic_module_type_lb_config_envoy_ptr lb_config_envoy_ptr, size_t id,
    envoy_dynamic_module_type_module_buffer* label_values, size_t label_values_length,
    uint64_t value);

/**
 * envoy_dynamic_module_callback_lb_config_decrement_gauge is called by the module to decrease the
 * value of a previously defined gauge.
 *
 * @param lb_config_envoy_ptr is the pointer to the DynamicModuleLbConfig.
 * @param id is the ID of the gauge previously defined using the config.
 * @param label_values is the values of the labels to be decreased.
 * NOTE: label_values could be null if the label_values_length is 0.
 * @param label_values_length is the length of the label_values.
 * NOTE: label_values_length could be 0 if there are no labels. **THE LENGTH MUST MATCH THE
 * LABEL NAMES DEFINED DURING GAUGE DEFINITION.**
 * @param value is the value to decrease the gauge by.
 * @return the result of the operation.
 */
envoy_dynamic_module_type_metrics_result envoy_dynamic_module_callback_lb_config_decrement_gauge(
    envoy_dynamic_module_type_lb_config_envoy_ptr lb_config_envoy_ptr, size_t id,
    envoy_dynamic_module_type_module_buffer* label_values, size_t label_values_length,
    uint64_t value);

/**
 * envoy_dynamic_module_callback_lb_config_define_histogram is called by the module during
 * initialization to create a template for generating Stats::Histograms with the given name and
 * labels during the lifecycle of the module.
 *
 * @param lb_config_envoy_ptr is the pointer to the DynamicModuleLbConfig in which the histogram
 * will be defined.
 * @param name is the name of the histogram to be defined.
 * @param label_names is the labels of the histogram to be defined.
 * NOTE: label names could be null if the label_names_length is 0.
 * @param label_names_length is the length of the label_names.
 * NOTE: label_names_length could be 0 if there are no labels.
 * @param histogram_id_ptr where the opaque ID that represents a unique metric will be stored.
 * This can be passed to envoy_dynamic_module_callback_lb_config_record_histogram_value together
 * with lb_config_envoy_ptr.
 * @return the result of the operation.
 */
envoy_dynamic_module_type_metrics_result envoy_dynamic_module_callback_lb_config_define_histogram(
    envoy_dynamic_module_type_lb_config_envoy_ptr lb_config_envoy_ptr,
    envoy_dynamic_module_type_module_buffer name,
    envoy_dynamic_module_type_module_buffer* label_names, size_t label_names_length,
    size_t* histogram_id_ptr);

/**
 * envoy_dynamic_module_callback_lb_config_record_histogram_value is called by the module to record
 * a value in a previously defined histogram.
 *
 * @param lb_config_envoy_ptr is the pointer to the DynamicModuleLbConfig.
 * @param id is the ID of the histogram previously defined using the config.
 * @param label_values is the values of the labels to be recorded.
 * NOTE: label_values could be null if the label_values_length is 0.
 * @param label_values_length is the length of the label_values.
 * NOTE: label_values_length could be 0 if there are no labels. **THE LENGTH MUST MATCH THE
 * LABEL NAMES DEFINED DURING HISTOGRAM DEFINITION.**
 * @param value is the value to record in the histogram.
 * @return the result of the operation.
 */
envoy_dynamic_module_type_metrics_result
envoy_dynamic_module_callback_lb_config_record_histogram_value(
    envoy_dynamic_module_type_lb_config_envoy_ptr lb_config_envoy_ptr, size_t id,
    envoy_dynamic_module_type_module_buffer* label_values, size_t label_values_length,
    uint64_t value);

// =============================================================================
// Matcher Types
// =============================================================================

/**
 * envoy_dynamic_module_type_matcher_config_envoy_ptr is a raw pointer to
 * the DynamicModuleInputMatcher class in Envoy.
 *
 * OWNERSHIP: Envoy owns the pointer.
 */
typedef void* envoy_dynamic_module_type_matcher_config_envoy_ptr;

/**
 * envoy_dynamic_module_type_matcher_config_module_ptr is a pointer to an in-module matcher
 * configuration.
 *
 * OWNERSHIP: The module is responsible for managing the lifetime of the pointer.
 */
typedef const void* envoy_dynamic_module_type_matcher_config_module_ptr;

/**
 * envoy_dynamic_module_type_matcher_input_envoy_ptr is a raw pointer to the matcher input in Envoy.
 * This represents the matching data available during a single match evaluation.
 *
 * OWNERSHIP: Envoy owns the pointer. Valid only during the match event hook.
 */
typedef void* envoy_dynamic_module_type_matcher_input_envoy_ptr;

// =============================================================================
// Matcher Event Hooks
// =============================================================================

/**
 * envoy_dynamic_module_on_matcher_config_new is called when a new matcher configuration
 * is created. This is called on the main thread.
 *
 * @param config_envoy_ptr is the pointer to the DynamicModuleInputMatcher object.
 * @param name is the matcher config name.
 * @param config is the configuration for the matcher.
 * @return a pointer to the in-module matcher configuration. Returning nullptr
 *         indicates a failure to initialize the module, and the configuration will be rejected.
 */
envoy_dynamic_module_type_matcher_config_module_ptr envoy_dynamic_module_on_matcher_config_new(
    envoy_dynamic_module_type_matcher_config_envoy_ptr config_envoy_ptr,
    envoy_dynamic_module_type_envoy_buffer name, envoy_dynamic_module_type_envoy_buffer config);

/**
 * envoy_dynamic_module_on_matcher_config_destroy is called when the matcher configuration
 * is destroyed.
 *
 * @param config_module_ptr is a pointer to the in-module matcher configuration.
 */
void envoy_dynamic_module_on_matcher_config_destroy(
    envoy_dynamic_module_type_matcher_config_module_ptr config_module_ptr);

/**
 * envoy_dynamic_module_on_matcher_match is called when a match evaluation occurs.
 * This is called on worker threads.
 *
 * The matcher_input_envoy_ptr is only valid during this callback. The module must not store
 * this pointer or use it after the callback returns. The module can use the matcher
 * callbacks (e.g. envoy_dynamic_module_callback_matcher_get_header_value) to access the
 * matching data during this callback.
 *
 * @param config_module_ptr is the pointer to the in-module matcher configuration.
 * @param matcher_input_envoy_ptr is the pointer to the Envoy matcher input (valid during this
 *        call only).
 * @return true if the input matches, false otherwise.
 */
bool envoy_dynamic_module_on_matcher_match(
    envoy_dynamic_module_type_matcher_config_module_ptr config_module_ptr,
    envoy_dynamic_module_type_matcher_input_envoy_ptr matcher_input_envoy_ptr);

// =============================================================================
// Matcher Callbacks
// =============================================================================

/**
 * Get the number of headers in the specified header map.
 *
 * @param matcher_input_envoy_ptr is the pointer to the matcher input.
 * @param header_type is the type of header map to access. Supported types are RequestHeader,
 *        ResponseHeader, and ResponseTrailer.
 * @return the number of headers, or 0 if the header map is not available.
 */
size_t envoy_dynamic_module_callback_matcher_get_headers_size(
    envoy_dynamic_module_type_matcher_input_envoy_ptr matcher_input_envoy_ptr,
    envoy_dynamic_module_type_http_header_type header_type);

/**
 * Get all headers from the specified header map.
 *
 * PRECONDITION: The module must ensure that result_headers is valid and has enough length to
 * store all the headers. Use envoy_dynamic_module_callback_matcher_get_headers_size to get
 * the number of headers before calling this function.
 *
 * @param matcher_input_envoy_ptr is the pointer to the matcher input.
 * @param header_type is the type of header map to access.
 * @param result_headers is the pointer to the array where headers will be stored.
 * @return true if the operation is successful, false otherwise.
 */
bool envoy_dynamic_module_callback_matcher_get_headers(
    envoy_dynamic_module_type_matcher_input_envoy_ptr matcher_input_envoy_ptr,
    envoy_dynamic_module_type_http_header_type header_type,
    envoy_dynamic_module_type_envoy_http_header* result_headers);

/**
 * Get a specific header value by key.
 *
 * Since a header can have multiple values, the index is used to get the specific value.
 * This returns the total number of values for the given key via total_count_out, so it can
 * be used to iterate over all values by starting from 0 and incrementing the index.
 *
 * @param matcher_input_envoy_ptr is the pointer to the matcher input.
 * @param header_type is the type of header map to access.
 * @param key is the key of the header to look up.
 * @param result is the buffer where the header value will be stored.
 * @param index is the index of the header value in the list of values for the given key.
 * @param total_count_out is the pointer to the variable where the total number of values for
 *        the given key will be stored. This parameter is optional and can be null.
 * @return true if the header value is found, false otherwise.
 */
bool envoy_dynamic_module_callback_matcher_get_header_value(
    envoy_dynamic_module_type_matcher_input_envoy_ptr matcher_input_envoy_ptr,
    envoy_dynamic_module_type_http_header_type header_type,
    envoy_dynamic_module_type_module_buffer key, envoy_dynamic_module_type_envoy_buffer* result,
    size_t index, size_t* total_count_out);

// =============================================================================
// ============================ Cert Validator ==================================
// =============================================================================
//
// This extension enables custom TLS certificate validation via dynamic modules.
// It integrates with Envoy's custom_validator_config in CertificateValidationContext,
// registered under the envoy.tls.cert_validator category.
//
// The module receives DER-encoded certificates during validation and returns
// a result indicating success or failure with optional TLS alert and error details.

// =============================================================================
// Cert Validator Types
// =============================================================================

/**
 * envoy_dynamic_module_type_cert_validator_config_envoy_ptr is a pointer to the
 * DynamicModuleCertValidatorConfig object in Envoy. This is passed to the module during config
 * creation and cert chain verification.
 *
 * OWNERSHIP: Envoy owns this object.
 */
typedef void* envoy_dynamic_module_type_cert_validator_config_envoy_ptr;

/**
 * envoy_dynamic_module_type_cert_validator_config_module_ptr is a pointer to the in-module cert
 * validator configuration created and owned by the module.
 *
 * OWNERSHIP: Module owns this pointer.
 */
typedef const void* envoy_dynamic_module_type_cert_validator_config_module_ptr;

/**
 * envoy_dynamic_module_type_cert_validator_validation_status represents the status of the
 * certificate chain validation. This corresponds to ValidationResults::ValidationStatus in
 * cert_validator.h.
 *
 * Note: Pending (asynchronous) validation is not supported.
 */
typedef enum envoy_dynamic_module_type_cert_validator_validation_status {
  envoy_dynamic_module_type_cert_validator_validation_status_Successful = 0,
  envoy_dynamic_module_type_cert_validator_validation_status_Failed = 1,
} envoy_dynamic_module_type_cert_validator_validation_status;

/**
 * envoy_dynamic_module_type_cert_validator_client_validation_status represents the detailed client
 * validation status. This corresponds to Ssl::ClientValidationStatus in
 * ssl_socket_extended_info.h.
 */
typedef enum envoy_dynamic_module_type_cert_validator_client_validation_status {
  envoy_dynamic_module_type_cert_validator_client_validation_status_NotValidated = 0,
  envoy_dynamic_module_type_cert_validator_client_validation_status_NoClientCertificate = 1,
  envoy_dynamic_module_type_cert_validator_client_validation_status_Validated = 2,
  envoy_dynamic_module_type_cert_validator_client_validation_status_Failed = 3,
} envoy_dynamic_module_type_cert_validator_client_validation_status;

/**
 * envoy_dynamic_module_type_cert_validator_validation_result is the result of a certificate chain
 * verification. Returned by the envoy_dynamic_module_on_cert_validator_do_verify_cert_chain event
 * hook.
 *
 * Error details, if any, should be set via the
 * envoy_dynamic_module_callback_cert_validator_set_error_details callback before returning.
 */
typedef struct envoy_dynamic_module_type_cert_validator_validation_result {
  // The overall validation status (Successful or Failed).
  envoy_dynamic_module_type_cert_validator_validation_status status;
  // The detailed client validation status.
  envoy_dynamic_module_type_cert_validator_client_validation_status detailed_status;
  // The TLS alert code to send on failure (e.g. SSL_AD_BAD_CERTIFICATE).
  uint8_t tls_alert;
  // Whether the tls_alert field is set.
  bool has_tls_alert;
} envoy_dynamic_module_type_cert_validator_validation_result;

// =============================================================================
// Cert Validator Event Hooks
// =============================================================================

/**
 * envoy_dynamic_module_on_cert_validator_config_new is called by the main thread when the cert
 * validator config is loaded. The function returns a
 * envoy_dynamic_module_type_cert_validator_config_module_ptr for given name and config.
 *
 * @param config_envoy_ptr is the pointer to the DynamicModuleCertValidatorConfig object for the
 * corresponding config.
 * @param name is the name of the validator owned by Envoy.
 * @param config is the configuration for the module owned by Envoy.
 * @return envoy_dynamic_module_type_cert_validator_config_module_ptr is the pointer to the
 * in-module cert validator configuration. Returning nullptr indicates a failure to initialize the
 * module. When it fails, the cert validator configuration will be rejected.
 */
envoy_dynamic_module_type_cert_validator_config_module_ptr
envoy_dynamic_module_on_cert_validator_config_new(
    envoy_dynamic_module_type_cert_validator_config_envoy_ptr config_envoy_ptr,
    envoy_dynamic_module_type_envoy_buffer name, envoy_dynamic_module_type_envoy_buffer config);

/**
 * envoy_dynamic_module_on_cert_validator_config_destroy is called when the cert validator
 * configuration is destroyed in Envoy. The module should release any resources associated with
 * the corresponding in-module cert validator configuration.
 *
 * @param config_module_ptr is a pointer to the in-module cert validator configuration whose
 * corresponding Envoy cert validator configuration is being destroyed.
 */
void envoy_dynamic_module_on_cert_validator_config_destroy(
    envoy_dynamic_module_type_cert_validator_config_module_ptr config_module_ptr);

/**
 * envoy_dynamic_module_on_cert_validator_do_verify_cert_chain is called to verify a certificate
 * chain during a TLS handshake. The certificates are provided as DER-encoded buffers. The first
 * certificate (index 0) is the leaf certificate.
 *
 * The certs array and its buffer contents are owned by Envoy and are valid only for the duration
 * of this event hook call.
 *
 * @param config_envoy_ptr is the pointer to the DynamicModuleCertValidatorConfig object.
 * @param config_module_ptr is the pointer to the in-module cert validator configuration.
 * @param certs is an array of DER-encoded certificate buffers.
 * @param certs_count is the number of certificates in the array.
 * @param host_name is the SNI host name for validation.
 * @param is_server is true if the validation is on the server side (validating client certs).
 * @return envoy_dynamic_module_type_cert_validator_validation_result is the validation result.
 */
envoy_dynamic_module_type_cert_validator_validation_result
envoy_dynamic_module_on_cert_validator_do_verify_cert_chain(
    envoy_dynamic_module_type_cert_validator_config_envoy_ptr config_envoy_ptr,
    envoy_dynamic_module_type_cert_validator_config_module_ptr config_module_ptr,
    envoy_dynamic_module_type_envoy_buffer* certs, size_t certs_count,
    envoy_dynamic_module_type_envoy_buffer host_name, bool is_server);

/**
 * envoy_dynamic_module_on_cert_validator_get_ssl_verify_mode is called during SSL context
 * initialization to get the SSL verify mode flags that should be applied to SSL contexts.
 *
 * The return value should be a combination of SSL_VERIFY_* flags (e.g. SSL_VERIFY_PEER,
 * SSL_VERIFY_FAIL_IF_NO_PEER_CERT). Returning 0 means SSL_VERIFY_NONE.
 *
 * @param config_module_ptr is the pointer to the in-module cert validator configuration.
 * @param handshaker_provides_certificates is true if the handshaker provides certificates itself.
 * @return int the SSL verify mode flags.
 */
int envoy_dynamic_module_on_cert_validator_get_ssl_verify_mode(
    envoy_dynamic_module_type_cert_validator_config_module_ptr config_module_ptr,
    bool handshaker_provides_certificates);

/**
 * envoy_dynamic_module_on_cert_validator_update_digest is called to contribute to the session
 * context hash. The module should provide bytes that uniquely identify its validation configuration
 * so that configuration changes invalidate existing TLS sessions. The output buffer must remain
 * valid until the end of this event hook.
 *
 * @param config_module_ptr is the pointer to the in-module cert validator configuration.
 * @param out_data is a pointer to a buffer that the module should fill with the digest data.
 * The module should set the ptr and length fields.
 */
void envoy_dynamic_module_on_cert_validator_update_digest(
    envoy_dynamic_module_type_cert_validator_config_module_ptr config_module_ptr,
    envoy_dynamic_module_type_module_buffer* out_data);

// =============================================================================
// Cert Validator Callbacks
// =============================================================================

/**
 * envoy_dynamic_module_callback_cert_validator_set_error_details is called by the module during
 * envoy_dynamic_module_on_cert_validator_do_verify_cert_chain to set error details for a failed
 * validation. Envoy copies the provided buffer immediately, so the module does not need to keep
 * the buffer alive after this call returns.
 *
 * This must only be called from within the do_verify_cert_chain event hook.
 *
 * @param config_envoy_ptr is the pointer to the DynamicModuleCertValidatorConfig object.
 * @param error_details is the error details string owned by the module.
 */
void envoy_dynamic_module_callback_cert_validator_set_error_details(
    envoy_dynamic_module_type_cert_validator_config_envoy_ptr config_envoy_ptr,
    envoy_dynamic_module_type_module_buffer error_details);

// ------------------------- Filter State Operations ---------------------------

/**
 * envoy_dynamic_module_callback_cert_validator_set_filter_state is called by the module to
 * set a string value in filter state with Connection life span. This must only be called from
 * within the do_verify_cert_chain event hook.
 *
 * @param config_envoy_ptr is the pointer to the DynamicModuleCertValidatorConfig object.
 * @param key is the key string owned by the module.
 * @param value is the value string owned by the module.
 * @return true if the operation was successful, false otherwise (e.g. no connection context
 * available or the key already exists and is read-only).
 */
bool envoy_dynamic_module_callback_cert_validator_set_filter_state(
    envoy_dynamic_module_type_cert_validator_config_envoy_ptr config_envoy_ptr,
    envoy_dynamic_module_type_module_buffer key, envoy_dynamic_module_type_module_buffer value);

/**
 * envoy_dynamic_module_callback_cert_validator_get_filter_state is called by the module to
 * get a string value from filter state. This must only be called from within the
 * do_verify_cert_chain event hook.
 *
 * @param config_envoy_ptr is the pointer to the DynamicModuleCertValidatorConfig object.
 * @param key is the key string owned by the module.
 * @param value_out is the output buffer where the value owned by Envoy will be stored.
 * @return true if the value was found, false otherwise.
 */
bool envoy_dynamic_module_callback_cert_validator_get_filter_state(
    envoy_dynamic_module_type_cert_validator_config_envoy_ptr config_envoy_ptr,
    envoy_dynamic_module_type_module_buffer key, envoy_dynamic_module_type_envoy_buffer* value_out);

// =============================================================================
// ========================= Upstream HTTP TCP Bridge ===========================
// =============================================================================
//
// This extension enables custom HTTP-to-TCP protocol bridging via dynamic modules.
// It implements the Router::GenericConnPoolFactory interface, allowing modules to
// transform HTTP requests into raw TCP data for upstream connections and convert
// TCP responses back into HTTP responses.
//
// The module receives HTTP request headers/body/trailers during the encode path
// and raw TCP response data during the decode path. The module uses callbacks to
// read request headers, manipulate request buffers (data sent upstream), and build
// HTTP responses (headers, body, trailers) from TCP data.

// =============================================================================
// Upstream HTTP TCP Bridge Types
// =============================================================================

/**
 * envoy_dynamic_module_type_upstream_http_tcp_bridge_config_envoy_ptr is a pointer to the
 * BridgeConfig object in Envoy. This is passed to the module during config creation for
 * future extensibility.
 *
 * OWNERSHIP: Envoy owns this object.
 */
typedef void* envoy_dynamic_module_type_upstream_http_tcp_bridge_config_envoy_ptr;

/**
 * envoy_dynamic_module_type_upstream_http_tcp_bridge_config_module_ptr is a pointer to the
 * in-module bridge configuration created and owned by the module.
 *
 * OWNERSHIP: Module owns this pointer.
 */
typedef const void* envoy_dynamic_module_type_upstream_http_tcp_bridge_config_module_ptr;

/**
 * envoy_dynamic_module_type_upstream_http_tcp_bridge_envoy_ptr is a pointer to the
 * HttpTcpBridge object in Envoy. This is passed to the module for each per-request bridge
 * instance and is used as the context for all callback invocations.
 *
 * OWNERSHIP: Envoy owns this object.
 */
typedef void* envoy_dynamic_module_type_upstream_http_tcp_bridge_envoy_ptr;

/**
 * envoy_dynamic_module_type_upstream_http_tcp_bridge_module_ptr is a pointer to the
 * in-module per-request bridge instance created and owned by the module.
 *
 * OWNERSHIP: Module owns this pointer.
 */
typedef const void* envoy_dynamic_module_type_upstream_http_tcp_bridge_module_ptr;

// =============================================================================
// Upstream HTTP TCP Bridge Event Hooks
// =============================================================================

/**
 * envoy_dynamic_module_on_upstream_http_tcp_bridge_config_new is called by the main thread when
 * the bridge configuration is loaded. The function returns a
 * envoy_dynamic_module_type_upstream_http_tcp_bridge_config_module_ptr for given name and config.
 *
 * @param config_envoy_ptr is the pointer to the BridgeConfig object for the corresponding config.
 * @param name is the name of the bridge owned by Envoy.
 * @param config is the configuration for the module owned by Envoy.
 * @return envoy_dynamic_module_type_upstream_http_tcp_bridge_config_module_ptr is the pointer to
 * the in-module bridge configuration. Returning nullptr indicates a failure to initialize the
 * module. When it fails, the bridge configuration will be rejected.
 */
envoy_dynamic_module_type_upstream_http_tcp_bridge_config_module_ptr
envoy_dynamic_module_on_upstream_http_tcp_bridge_config_new(
    envoy_dynamic_module_type_upstream_http_tcp_bridge_config_envoy_ptr config_envoy_ptr,
    envoy_dynamic_module_type_envoy_buffer name, envoy_dynamic_module_type_envoy_buffer config);

/**
 * envoy_dynamic_module_on_upstream_http_tcp_bridge_config_destroy is called when the bridge
 * configuration is destroyed in Envoy. The module should release any resources associated with
 * the corresponding in-module bridge configuration.
 *
 * @param config_module_ptr is a pointer to the in-module bridge configuration whose corresponding
 * Envoy bridge configuration is being destroyed.
 */
void envoy_dynamic_module_on_upstream_http_tcp_bridge_config_destroy(
    envoy_dynamic_module_type_upstream_http_tcp_bridge_config_module_ptr config_module_ptr);

/**
 * envoy_dynamic_module_on_upstream_http_tcp_bridge_new is called when a new per-request bridge
 * instance is created. This happens for each HTTP request that is routed to a cluster configured
 * with this upstream bridge.
 *
 * @param config_module_ptr is the pointer to the in-module bridge configuration.
 * @param bridge_envoy_ptr is the pointer to the HttpTcpBridge object in Envoy.
 * @return envoy_dynamic_module_type_upstream_http_tcp_bridge_module_ptr is the pointer to the
 * in-module per-request bridge instance. Returning nullptr indicates a failure to create the
 * bridge.
 */
envoy_dynamic_module_type_upstream_http_tcp_bridge_module_ptr
envoy_dynamic_module_on_upstream_http_tcp_bridge_new(
    envoy_dynamic_module_type_upstream_http_tcp_bridge_config_module_ptr config_module_ptr,
    envoy_dynamic_module_type_upstream_http_tcp_bridge_envoy_ptr bridge_envoy_ptr);

/**
 * envoy_dynamic_module_on_upstream_http_tcp_bridge_encode_headers is called when the HTTP request
 * headers are being encoded for the upstream. The module can read request headers via header
 * callbacks and use send_upstream_data or send_response to act on the request.
 *
 * @param bridge_envoy_ptr is the pointer to the HttpTcpBridge object.
 * @param bridge_module_ptr is the pointer to the in-module per-request bridge instance.
 * @param end_of_stream is true if this is the final frame (header-only request).
 */
void envoy_dynamic_module_on_upstream_http_tcp_bridge_encode_headers(
    envoy_dynamic_module_type_upstream_http_tcp_bridge_envoy_ptr bridge_envoy_ptr,
    envoy_dynamic_module_type_upstream_http_tcp_bridge_module_ptr bridge_module_ptr,
    bool end_of_stream);

/**
 * envoy_dynamic_module_on_upstream_http_tcp_bridge_encode_data is called when the HTTP request
 * body data is being encoded for the upstream. The module can read the current request body data
 * via get_request_buffer and use send_upstream_data to forward data to the TCP upstream.
 *
 * @param bridge_envoy_ptr is the pointer to the HttpTcpBridge object.
 * @param bridge_module_ptr is the pointer to the in-module per-request bridge instance.
 * @param end_of_stream is true if this is the final data frame.
 */
void envoy_dynamic_module_on_upstream_http_tcp_bridge_encode_data(
    envoy_dynamic_module_type_upstream_http_tcp_bridge_envoy_ptr bridge_envoy_ptr,
    envoy_dynamic_module_type_upstream_http_tcp_bridge_module_ptr bridge_module_ptr,
    bool end_of_stream);

/**
 * envoy_dynamic_module_on_upstream_http_tcp_bridge_encode_trailers is called when the HTTP request
 * trailers are being encoded for the upstream. The module can use send_upstream_data to forward
 * any remaining data to the TCP upstream.
 *
 * @param bridge_envoy_ptr is the pointer to the HttpTcpBridge object.
 * @param bridge_module_ptr is the pointer to the in-module per-request bridge instance.
 */
void envoy_dynamic_module_on_upstream_http_tcp_bridge_encode_trailers(
    envoy_dynamic_module_type_upstream_http_tcp_bridge_envoy_ptr bridge_envoy_ptr,
    envoy_dynamic_module_type_upstream_http_tcp_bridge_module_ptr bridge_module_ptr);

/**
 * envoy_dynamic_module_on_upstream_http_tcp_bridge_on_upstream_data is called when raw TCP data
 * is received from the upstream connection. The module should read the TCP data via
 * get_response_buffer, process it, and send the HTTP response using send_response_headers,
 * send_response_data, and send_response_trailers callbacks.
 *
 * @param bridge_envoy_ptr is the pointer to the HttpTcpBridge object.
 * @param bridge_module_ptr is the pointer to the in-module per-request bridge instance.
 * @param end_of_stream is true if the upstream connection has closed (no more data).
 */
void envoy_dynamic_module_on_upstream_http_tcp_bridge_on_upstream_data(
    envoy_dynamic_module_type_upstream_http_tcp_bridge_envoy_ptr bridge_envoy_ptr,
    envoy_dynamic_module_type_upstream_http_tcp_bridge_module_ptr bridge_module_ptr,
    bool end_of_stream);

/**
 * envoy_dynamic_module_on_upstream_http_tcp_bridge_destroy is called when the per-request bridge
 * instance is being destroyed. The module should release any resources associated with the
 * bridge instance.
 *
 * @param bridge_module_ptr is a pointer to the in-module per-request bridge instance.
 */
void envoy_dynamic_module_on_upstream_http_tcp_bridge_destroy(
    envoy_dynamic_module_type_upstream_http_tcp_bridge_module_ptr bridge_module_ptr);

// =============================================================================
// Upstream HTTP TCP Bridge Callbacks
// =============================================================================

// ----------------------- Request Header Operations ---------------------------

/**
 * envoy_dynamic_module_callback_upstream_http_tcp_bridge_get_request_header is called by the
 * module to get a request header value by key. Since a header can have multiple values, the
 * index is used to get the specific value.
 *
 * @param bridge_envoy_ptr is the pointer to the HttpTcpBridge object.
 * @param key is the key of the header to look up.
 * @param result is the buffer where the header value will be stored.
 * @param index is the index of the header value in the list of values for the given key.
 * @param total_count_out is the pointer to the variable where the total number of values for
 *        the given key will be stored. This parameter is optional and can be null.
 * @return true if the header value is found, false otherwise.
 */
bool envoy_dynamic_module_callback_upstream_http_tcp_bridge_get_request_header(
    envoy_dynamic_module_type_upstream_http_tcp_bridge_envoy_ptr bridge_envoy_ptr,
    envoy_dynamic_module_type_module_buffer key, envoy_dynamic_module_type_envoy_buffer* result,
    size_t index, size_t* total_count_out);

/**
 * envoy_dynamic_module_callback_upstream_http_tcp_bridge_get_request_headers_size is called by
 * the module to get the number of request headers.
 *
 * @param bridge_envoy_ptr is the pointer to the HttpTcpBridge object.
 * @return the number of request headers. 0 if there are no headers or headers could not be
 * retrieved.
 */
size_t envoy_dynamic_module_callback_upstream_http_tcp_bridge_get_request_headers_size(
    envoy_dynamic_module_type_upstream_http_tcp_bridge_envoy_ptr bridge_envoy_ptr);

/**
 * envoy_dynamic_module_callback_upstream_http_tcp_bridge_get_request_headers is called by the
 * module to get all request headers.
 *
 * PRECONDITION: The module must ensure that result_headers is valid and has enough length to
 * store all the headers. Use get_request_headers_size to get the number of headers first.
 *
 * @param bridge_envoy_ptr is the pointer to the HttpTcpBridge object.
 * @param result_headers is the pointer to the array where headers will be stored.
 * @return true if the operation is successful, false otherwise.
 */
bool envoy_dynamic_module_callback_upstream_http_tcp_bridge_get_request_headers(
    envoy_dynamic_module_type_upstream_http_tcp_bridge_envoy_ptr bridge_envoy_ptr,
    envoy_dynamic_module_type_envoy_http_header* result_headers);

// ----------------------- Request Buffer Operations ---------------------------

/**
 * envoy_dynamic_module_callback_upstream_http_tcp_bridge_get_request_buffer is called by the
 * module to get the current request body data as a series of buffer slices. During encode_data,
 * this contains the current body chunk. During encode_headers, the buffer is initially empty.
 *
 * @param bridge_envoy_ptr is the pointer to the HttpTcpBridge object.
 * @param result_buffer is the output array for buffer slices owned by Envoy.
 * @param result_buffer_length is the output for the number of slices.
 */
void envoy_dynamic_module_callback_upstream_http_tcp_bridge_get_request_buffer(
    envoy_dynamic_module_type_upstream_http_tcp_bridge_envoy_ptr bridge_envoy_ptr,
    envoy_dynamic_module_type_envoy_buffer* result_buffer, size_t* result_buffer_length);

// ----------------------- Response Buffer Operations --------------------------

/**
 * envoy_dynamic_module_callback_upstream_http_tcp_bridge_get_response_buffer is called by the
 * module to get the raw TCP data received from the upstream connection as a series of buffer
 * slices. This is available during on_upstream_data.
 *
 * @param bridge_envoy_ptr is the pointer to the HttpTcpBridge object.
 * @param result_buffer is the output array for buffer slices owned by Envoy.
 * @param result_buffer_length is the output for the number of slices.
 */
void envoy_dynamic_module_callback_upstream_http_tcp_bridge_get_response_buffer(
    envoy_dynamic_module_type_upstream_http_tcp_bridge_envoy_ptr bridge_envoy_ptr,
    envoy_dynamic_module_type_envoy_buffer* result_buffer, size_t* result_buffer_length);

// ----------------------- Send Upstream Data ----------------------------------

/**
 * envoy_dynamic_module_callback_upstream_http_tcp_bridge_send_upstream_data is called by the
 * module to send transformed data to the TCP upstream connection.
 *
 * @param bridge_envoy_ptr is the pointer to the HttpTcpBridge object.
 * @param data is the data to send, owned by the module. Envoy copies the data.
 * @param end_stream is true to half-close the upstream connection after writing.
 */
void envoy_dynamic_module_callback_upstream_http_tcp_bridge_send_upstream_data(
    envoy_dynamic_module_type_upstream_http_tcp_bridge_envoy_ptr bridge_envoy_ptr,
    envoy_dynamic_module_type_module_buffer data, bool end_stream);

// ----------------------- Send Response Operations ----------------------------

/**
 * envoy_dynamic_module_callback_upstream_http_tcp_bridge_send_response is called by the module
 * to send a complete local response to the downstream client, ending the stream. This is useful
 * for error responses or short-circuit replies that do not require upstream communication.
 *
 * @param bridge_envoy_ptr is the pointer to the HttpTcpBridge object.
 * @param status_code is the HTTP status code of the response.
 * @param headers_vector is the array of response headers owned by the module. Can be null.
 * @param headers_vector_size is the number of headers in the array.
 * @param body is the response body, owned by the module.
 */
void envoy_dynamic_module_callback_upstream_http_tcp_bridge_send_response(
    envoy_dynamic_module_type_upstream_http_tcp_bridge_envoy_ptr bridge_envoy_ptr,
    uint32_t status_code, envoy_dynamic_module_type_module_http_header* headers_vector,
    size_t headers_vector_size, envoy_dynamic_module_type_module_buffer body);

/**
 * envoy_dynamic_module_callback_upstream_http_tcp_bridge_send_response_headers is called by the
 * module to send response headers to the downstream client, optionally ending the stream.
 *
 * @param bridge_envoy_ptr is the pointer to the HttpTcpBridge object.
 * @param status_code is the HTTP status code of the response.
 * @param headers_vector is the array of response headers owned by the module. Can be null.
 * @param headers_vector_size is the number of headers in the array.
 * @param end_stream is true to end the stream after sending headers.
 */
void envoy_dynamic_module_callback_upstream_http_tcp_bridge_send_response_headers(
    envoy_dynamic_module_type_upstream_http_tcp_bridge_envoy_ptr bridge_envoy_ptr,
    uint32_t status_code, envoy_dynamic_module_type_module_http_header* headers_vector,
    size_t headers_vector_size, bool end_stream);

/**
 * envoy_dynamic_module_callback_upstream_http_tcp_bridge_send_response_data is called by the
 * module to send response body data to the downstream client, optionally ending the stream.
 * This can be called multiple times to stream data.
 *
 * @param bridge_envoy_ptr is the pointer to the HttpTcpBridge object.
 * @param data is the response body data, owned by the module. Envoy copies the data.
 * @param end_stream is true to end the stream after sending data.
 */
void envoy_dynamic_module_callback_upstream_http_tcp_bridge_send_response_data(
    envoy_dynamic_module_type_upstream_http_tcp_bridge_envoy_ptr bridge_envoy_ptr,
    envoy_dynamic_module_type_module_buffer data, bool end_stream);

/**
 * envoy_dynamic_module_callback_upstream_http_tcp_bridge_send_response_trailers is called by the
 * module to send response trailers to the downstream client, ending the stream.
 *
 * @param bridge_envoy_ptr is the pointer to the HttpTcpBridge object.
 * @param trailers_vector is the array of response trailers owned by the module.
 * @param trailers_vector_size is the number of trailers in the array.
 */
void envoy_dynamic_module_callback_upstream_http_tcp_bridge_send_response_trailers(
    envoy_dynamic_module_type_upstream_http_tcp_bridge_envoy_ptr bridge_envoy_ptr,
    envoy_dynamic_module_type_module_http_header* trailers_vector, size_t trailers_vector_size);

// =============================================================================
// ================================== Tracer ===================================
// =============================================================================
//
// This extension enables custom distributed tracing via dynamic modules.
// It implements the Tracing::Driver and Tracing::Span interfaces, allowing
// modules to create spans, propagate trace context, set tags, log events,
// and report traces to arbitrary backends.
//
// The module receives trace context (headers) from incoming requests during
// span creation and can inject trace context into outgoing requests for
// propagation. The module controls all span lifecycle operations.

// =============================================================================
// Tracer Types
// =============================================================================

/**
 * envoy_dynamic_module_type_tracer_config_envoy_ptr is a pointer to the
 * DynamicModuleTracerConfig object in Envoy. This is passed to the module during config creation.
 *
 * OWNERSHIP: Envoy owns this object.
 */
typedef void* envoy_dynamic_module_type_tracer_config_envoy_ptr;

/**
 * envoy_dynamic_module_type_tracer_config_module_ptr is a pointer to the
 * in-module tracer configuration created and owned by the module.
 *
 * OWNERSHIP: Module owns this pointer.
 */
typedef const void* envoy_dynamic_module_type_tracer_config_module_ptr;

/**
 * envoy_dynamic_module_type_tracer_span_envoy_ptr is a pointer to the
 * DynamicModuleSpan object in Envoy. This is used as context for trace context
 * access callbacks during startSpan and injectContext.
 *
 * OWNERSHIP: Envoy owns this object.
 */
typedef void* envoy_dynamic_module_type_tracer_span_envoy_ptr;

/**
 * envoy_dynamic_module_type_tracer_span_module_ptr is a pointer to the
 * in-module span instance created and owned by the module.
 *
 * OWNERSHIP: Module owns this pointer.
 */
typedef const void* envoy_dynamic_module_type_tracer_span_module_ptr;

/**
 * envoy_dynamic_module_type_trace_reason corresponds to Envoy's Tracing::Reason enum.
 */
typedef enum envoy_dynamic_module_type_trace_reason {
  envoy_dynamic_module_type_trace_reason_NotTraceable,
  envoy_dynamic_module_type_trace_reason_HealthCheck,
  envoy_dynamic_module_type_trace_reason_Sampling,
  envoy_dynamic_module_type_trace_reason_ServiceForced,
  envoy_dynamic_module_type_trace_reason_ClientForced,
} envoy_dynamic_module_type_trace_reason;

// =============================================================================
// Tracer Event Hooks
// =============================================================================

/**
 * envoy_dynamic_module_on_tracer_config_new is called by the main thread when the tracer
 * configuration is loaded. The function returns a module-side config pointer for the given name
 * and config.
 *
 * @param config_envoy_ptr is the pointer to the DynamicModuleTracerConfig object.
 * @param name is the tracer name owned by Envoy.
 * @param config is the configuration for the module owned by Envoy.
 * @return envoy_dynamic_module_type_tracer_config_module_ptr is the pointer to the in-module
 * tracer configuration. Returning nullptr indicates a failure to initialize the module.
 * When it fails, the tracer configuration will be rejected.
 */
envoy_dynamic_module_type_tracer_config_module_ptr envoy_dynamic_module_on_tracer_config_new(
    envoy_dynamic_module_type_tracer_config_envoy_ptr config_envoy_ptr,
    envoy_dynamic_module_type_envoy_buffer name, envoy_dynamic_module_type_envoy_buffer config);

/**
 * envoy_dynamic_module_on_tracer_config_destroy is called when the tracer configuration is
 * destroyed in Envoy. The module should release any resources associated with the corresponding
 * in-module tracer configuration.
 *
 * @param config_module_ptr is a pointer to the in-module tracer configuration whose corresponding
 * Envoy tracer configuration is being destroyed.
 */
void envoy_dynamic_module_on_tracer_config_destroy(
    envoy_dynamic_module_type_tracer_config_module_ptr config_module_ptr);

/**
 * envoy_dynamic_module_on_tracer_start_span is called when a new span needs to be started
 * for an incoming request. During this call, the module can use trace context callbacks
 * (get/set/remove) on span_envoy_ptr to read incoming propagation headers.
 *
 * @param config_module_ptr is the pointer to the in-module tracer configuration.
 * @param span_envoy_ptr is the pointer to the DynamicModuleSpan object in Envoy. This is used
 * as context for trace context callbacks.
 * @param operation_name is the operation name for this span.
 * @param traced is true if this request should be traced based on Envoy's sampling decision.
 * @param reason is the reason for the tracing decision.
 * @return envoy_dynamic_module_type_tracer_span_module_ptr is the pointer to the in-module
 * span instance. Returning nullptr results in a NullSpan being used.
 */
envoy_dynamic_module_type_tracer_span_module_ptr envoy_dynamic_module_on_tracer_start_span(
    envoy_dynamic_module_type_tracer_config_module_ptr config_module_ptr,
    envoy_dynamic_module_type_tracer_span_envoy_ptr span_envoy_ptr,
    envoy_dynamic_module_type_envoy_buffer operation_name, bool traced,
    envoy_dynamic_module_type_trace_reason reason);

/**
 * envoy_dynamic_module_on_tracer_span_set_operation is called to update the span's operation name.
 *
 * @param span_module_ptr is the pointer to the in-module span instance.
 * @param operation is the new operation name.
 */
void envoy_dynamic_module_on_tracer_span_set_operation(
    envoy_dynamic_module_type_tracer_span_module_ptr span_module_ptr,
    envoy_dynamic_module_type_envoy_buffer operation);

/**
 * envoy_dynamic_module_on_tracer_span_set_tag is called to set a tag on the span.
 *
 * @param span_module_ptr is the pointer to the in-module span instance.
 * @param key is the tag key.
 * @param value is the tag value.
 */
void envoy_dynamic_module_on_tracer_span_set_tag(
    envoy_dynamic_module_type_tracer_span_module_ptr span_module_ptr,
    envoy_dynamic_module_type_envoy_buffer key, envoy_dynamic_module_type_envoy_buffer value);

/**
 * envoy_dynamic_module_on_tracer_span_log is called to record a log event on the span.
 *
 * @param span_module_ptr is the pointer to the in-module span instance.
 * @param timestamp_ns is the event timestamp in nanoseconds since epoch.
 * @param event is the event description.
 */
void envoy_dynamic_module_on_tracer_span_log(
    envoy_dynamic_module_type_tracer_span_module_ptr span_module_ptr, int64_t timestamp_ns,
    envoy_dynamic_module_type_envoy_buffer event);

/**
 * envoy_dynamic_module_on_tracer_span_finish is called to finish the span and report it.
 *
 * @param span_module_ptr is the pointer to the in-module span instance.
 */
void envoy_dynamic_module_on_tracer_span_finish(
    envoy_dynamic_module_type_tracer_span_module_ptr span_module_ptr);

/**
 * envoy_dynamic_module_on_tracer_span_inject_context is called when Envoy needs to propagate
 * trace context to an upstream request. During this call, the module can use trace context
 * callbacks (get/set/remove) on span_envoy_ptr to write propagation headers.
 *
 * @param span_module_ptr is the pointer to the in-module span instance.
 * @param span_envoy_ptr is the pointer to the DynamicModuleSpan object with the outgoing
 * trace context set. The module should use set/remove callbacks on this pointer.
 */
void envoy_dynamic_module_on_tracer_span_inject_context(
    envoy_dynamic_module_type_tracer_span_module_ptr span_module_ptr,
    envoy_dynamic_module_type_tracer_span_envoy_ptr span_envoy_ptr);

/**
 * envoy_dynamic_module_on_tracer_span_spawn_child is called to create a child span from the
 * current span.
 *
 * @param span_module_ptr is the pointer to the parent in-module span instance.
 * @param name is the operation name for the child span.
 * @param start_time_ns is the start time in nanoseconds since epoch.
 * @return envoy_dynamic_module_type_tracer_span_module_ptr is the pointer to the child in-module
 * span instance. Returning nullptr results in a NullSpan being used for the child.
 */
envoy_dynamic_module_type_tracer_span_module_ptr envoy_dynamic_module_on_tracer_span_spawn_child(
    envoy_dynamic_module_type_tracer_span_module_ptr span_module_ptr,
    envoy_dynamic_module_type_envoy_buffer name, int64_t start_time_ns);

/**
 * envoy_dynamic_module_on_tracer_span_set_sampled is called to override the sampling decision.
 *
 * @param span_module_ptr is the pointer to the in-module span instance.
 * @param sampled is true if the span should be sampled/reported.
 */
void envoy_dynamic_module_on_tracer_span_set_sampled(
    envoy_dynamic_module_type_tracer_span_module_ptr span_module_ptr, bool sampled);

/**
 * envoy_dynamic_module_on_tracer_span_use_local_decision is called to query whether the span
 * uses Envoy's local sampling decision or its own.
 *
 * @param span_module_ptr is the pointer to the in-module span instance.
 * @return true if Envoy's sampling decision is used, false if the module has its own.
 */
bool envoy_dynamic_module_on_tracer_span_use_local_decision(
    envoy_dynamic_module_type_tracer_span_module_ptr span_module_ptr);

/**
 * envoy_dynamic_module_on_tracer_span_get_baggage is called to retrieve a baggage value by key.
 *
 * @param span_module_ptr is the pointer to the in-module span instance.
 * @param key is the baggage key.
 * @param value_out is the output buffer where the baggage value will be stored.
 * @return true if the baggage key was found, false otherwise.
 */
bool envoy_dynamic_module_on_tracer_span_get_baggage(
    envoy_dynamic_module_type_tracer_span_module_ptr span_module_ptr,
    envoy_dynamic_module_type_envoy_buffer key, envoy_dynamic_module_type_module_buffer* value_out);

/**
 * envoy_dynamic_module_on_tracer_span_set_baggage is called to set a baggage key/value pair.
 *
 * @param span_module_ptr is the pointer to the in-module span instance.
 * @param key is the baggage key.
 * @param value is the baggage value.
 */
void envoy_dynamic_module_on_tracer_span_set_baggage(
    envoy_dynamic_module_type_tracer_span_module_ptr span_module_ptr,
    envoy_dynamic_module_type_envoy_buffer key, envoy_dynamic_module_type_envoy_buffer value);

/**
 * envoy_dynamic_module_on_tracer_span_get_trace_id is called to retrieve the trace ID.
 *
 * @param span_module_ptr is the pointer to the in-module span instance.
 * @param value_out is the output buffer where the trace ID will be stored. The module must
 * ensure the underlying memory remains valid until the span is destroyed.
 * @return true if a trace ID is available, false otherwise.
 */
bool envoy_dynamic_module_on_tracer_span_get_trace_id(
    envoy_dynamic_module_type_tracer_span_module_ptr span_module_ptr,
    envoy_dynamic_module_type_module_buffer* value_out);

/**
 * envoy_dynamic_module_on_tracer_span_get_span_id is called to retrieve the span ID.
 *
 * @param span_module_ptr is the pointer to the in-module span instance.
 * @param value_out is the output buffer where the span ID will be stored. The module must
 * ensure the underlying memory remains valid until the span is destroyed.
 * @return true if a span ID is available, false otherwise.
 */
bool envoy_dynamic_module_on_tracer_span_get_span_id(
    envoy_dynamic_module_type_tracer_span_module_ptr span_module_ptr,
    envoy_dynamic_module_type_module_buffer* value_out);

/**
 * envoy_dynamic_module_on_tracer_span_destroy is called when the span is being destroyed.
 * The module should release any resources associated with the span.
 *
 * @param span_module_ptr is the pointer to the in-module span instance.
 */
void envoy_dynamic_module_on_tracer_span_destroy(
    envoy_dynamic_module_type_tracer_span_module_ptr span_module_ptr);

// =============================================================================
// Tracer Callbacks
// =============================================================================

/**
 * envoy_dynamic_module_callback_tracer_get_trace_context_value is called by the module to get
 * a trace context header value by key. This operates on the currently active trace context
 * (incoming during startSpan, outgoing during injectContext).
 *
 * @param span_envoy_ptr is the pointer to the DynamicModuleSpan object.
 * @param key is the header key to look up.
 * @param value_out is the buffer where the header value will be stored.
 * @return true if the header was found, false otherwise.
 */
bool envoy_dynamic_module_callback_tracer_get_trace_context_value(
    envoy_dynamic_module_type_tracer_span_envoy_ptr span_envoy_ptr,
    envoy_dynamic_module_type_module_buffer key, envoy_dynamic_module_type_envoy_buffer* value_out);

/**
 * envoy_dynamic_module_callback_tracer_set_trace_context_value is called by the module to set
 * a trace context header. This is typically used during injectContext to write propagation headers.
 *
 * @param span_envoy_ptr is the pointer to the DynamicModuleSpan object.
 * @param key is the header key to set.
 * @param value is the header value to set.
 */
void envoy_dynamic_module_callback_tracer_set_trace_context_value(
    envoy_dynamic_module_type_tracer_span_envoy_ptr span_envoy_ptr,
    envoy_dynamic_module_type_module_buffer key, envoy_dynamic_module_type_module_buffer value);

/**
 * envoy_dynamic_module_callback_tracer_remove_trace_context_value is called by the module to
 * remove a trace context header.
 *
 * @param span_envoy_ptr is the pointer to the DynamicModuleSpan object.
 * @param key is the header key to remove.
 */
void envoy_dynamic_module_callback_tracer_remove_trace_context_value(
    envoy_dynamic_module_type_tracer_span_envoy_ptr span_envoy_ptr,
    envoy_dynamic_module_type_module_buffer key);

/**
 * envoy_dynamic_module_callback_tracer_get_trace_context_protocol is called by the module to
 * get the protocol of the traceable stream (e.g., "HTTP/1.1", "HTTP/2").
 *
 * @param span_envoy_ptr is the pointer to the DynamicModuleSpan object.
 * @param value_out is the buffer where the protocol string will be stored.
 * @return true if the protocol is available, false otherwise.
 */
bool envoy_dynamic_module_callback_tracer_get_trace_context_protocol(
    envoy_dynamic_module_type_tracer_span_envoy_ptr span_envoy_ptr,
    envoy_dynamic_module_type_envoy_buffer* value_out);

/**
 * envoy_dynamic_module_callback_tracer_get_trace_context_host is called by the module to get
 * the host of the traceable stream.
 *
 * @param span_envoy_ptr is the pointer to the DynamicModuleSpan object.
 * @param value_out is the buffer where the host string will be stored.
 * @return true if the host is available, false otherwise.
 */
bool envoy_dynamic_module_callback_tracer_get_trace_context_host(
    envoy_dynamic_module_type_tracer_span_envoy_ptr span_envoy_ptr,
    envoy_dynamic_module_type_envoy_buffer* value_out);

/**
 * envoy_dynamic_module_callback_tracer_get_trace_context_path is called by the module to get
 * the path of the traceable stream.
 *
 * @param span_envoy_ptr is the pointer to the DynamicModuleSpan object.
 * @param value_out is the buffer where the path string will be stored.
 * @return true if the path is available, false otherwise.
 */
bool envoy_dynamic_module_callback_tracer_get_trace_context_path(
    envoy_dynamic_module_type_tracer_span_envoy_ptr span_envoy_ptr,
    envoy_dynamic_module_type_envoy_buffer* value_out);

/**
 * envoy_dynamic_module_callback_tracer_get_trace_context_method is called by the module to get
 * the method of the traceable stream.
 *
 * @param span_envoy_ptr is the pointer to the DynamicModuleSpan object.
 * @param value_out is the buffer where the method string will be stored.
 * @return true if the method is available, false otherwise.
 */
bool envoy_dynamic_module_callback_tracer_get_trace_context_method(
    envoy_dynamic_module_type_tracer_span_envoy_ptr span_envoy_ptr,
    envoy_dynamic_module_type_envoy_buffer* value_out);

/**
 * envoy_dynamic_module_callback_tracer_define_counter is called by the module during
 * initialization to create a template for generating Stats::Counters with the given name and
 * labels during the lifecycle of the module.
 *
 * @param config_envoy_ptr is the pointer to the DynamicModuleTracerConfig object.
 * @param name is the name of the counter to be defined.
 * @param label_names is the labels of the counter to be defined.
 * NOTE: label names could be null if the label_names_length is 0.
 * @param label_names_length is the length of the label_names.
 * NOTE: label_names_length could be 0 if there are no labels.
 * @param counter_id_ptr where the opaque ID that represents a unique metric will be stored. This
 * can be passed to envoy_dynamic_module_callback_tracer_increment_counter together with
 * config_envoy_ptr.
 * @return the result of the operation.
 */
envoy_dynamic_module_type_metrics_result envoy_dynamic_module_callback_tracer_define_counter(
    envoy_dynamic_module_type_tracer_config_envoy_ptr config_envoy_ptr,
    envoy_dynamic_module_type_module_buffer name,
    envoy_dynamic_module_type_module_buffer* label_names, size_t label_names_length,
    size_t* counter_id_ptr);

/**
 * envoy_dynamic_module_callback_tracer_define_gauge is called by the module during
 * initialization to create a template for generating Stats::Gauges with the given name and
 * labels during the lifecycle of the module.
 *
 * @param config_envoy_ptr is the pointer to the DynamicModuleTracerConfig object.
 * @param name is the name of the gauge to be defined.
 * @param label_names is the labels of the gauge to be defined.
 * NOTE: label names could be null if the label_names_length is 0.
 * @param label_names_length is the length of the label_names.
 * NOTE: label_names_length could be 0 if there are no labels.
 * @param gauge_id_ptr where the opaque ID that represents a unique metric will be stored.
 * @return the result of the operation.
 */
envoy_dynamic_module_type_metrics_result envoy_dynamic_module_callback_tracer_define_gauge(
    envoy_dynamic_module_type_tracer_config_envoy_ptr config_envoy_ptr,
    envoy_dynamic_module_type_module_buffer name,
    envoy_dynamic_module_type_module_buffer* label_names, size_t label_names_length,
    size_t* gauge_id_ptr);

/**
 * envoy_dynamic_module_callback_tracer_define_histogram is called by the module during
 * initialization to create a template for generating Stats::Histograms with the given name and
 * labels during the lifecycle of the module.
 *
 * @param config_envoy_ptr is the pointer to the DynamicModuleTracerConfig object.
 * @param name is the name of the histogram to be defined.
 * @param label_names is the labels of the histogram to be defined.
 * NOTE: label names could be null if the label_names_length is 0.
 * @param label_names_length is the length of the label_names.
 * NOTE: label_names_length could be 0 if there are no labels.
 * @param histogram_id_ptr where the opaque ID that represents a unique metric will be stored.
 * @return the result of the operation.
 */
envoy_dynamic_module_type_metrics_result envoy_dynamic_module_callback_tracer_define_histogram(
    envoy_dynamic_module_type_tracer_config_envoy_ptr config_envoy_ptr,
    envoy_dynamic_module_type_module_buffer name,
    envoy_dynamic_module_type_module_buffer* label_names, size_t label_names_length,
    size_t* histogram_id_ptr);

/**
 * envoy_dynamic_module_callback_tracer_increment_counter is called by the module to increment
 * a previously defined counter.
 *
 * @param config_envoy_ptr is the pointer to the DynamicModuleTracerConfig object.
 * @param id is the ID of the counter previously defined using the config.
 * @param label_values is the values of the labels to be incremented.
 * NOTE: label_values could be null if the label_values_length is 0.
 * @param label_values_length is the length of the label_values.
 * NOTE: label_values_length could be 0 if there are no labels. **THE LENGTH MUST MATCH THE
 * LABEL NAMES DEFINED DURING COUNTER DEFINITION.**
 * @param value is the value to increment the counter by.
 * @return the result of the operation.
 */
envoy_dynamic_module_type_metrics_result envoy_dynamic_module_callback_tracer_increment_counter(
    envoy_dynamic_module_type_tracer_config_envoy_ptr config_envoy_ptr, size_t id,
    envoy_dynamic_module_type_module_buffer* label_values, size_t label_values_length,
    uint64_t value);

/**
 * envoy_dynamic_module_callback_tracer_record_histogram_value is called by the module to
 * record a value for a previously defined histogram.
 *
 * @param config_envoy_ptr is the pointer to the DynamicModuleTracerConfig object.
 * @param id is the ID of the histogram previously defined using the config.
 * @param label_values is the values of the labels to be recorded.
 * NOTE: label_values could be null if the label_values_length is 0.
 * @param label_values_length is the length of the label_values.
 * NOTE: label_values_length could be 0 if there are no labels. **THE LENGTH MUST MATCH THE
 * LABEL NAMES DEFINED DURING HISTOGRAM DEFINITION.**
 * @param value is the value to record.
 * @return the result of the operation.
 */
envoy_dynamic_module_type_metrics_result
envoy_dynamic_module_callback_tracer_record_histogram_value(
    envoy_dynamic_module_type_tracer_config_envoy_ptr config_envoy_ptr, size_t id,
    envoy_dynamic_module_type_module_buffer* label_values, size_t label_values_length,
    uint64_t value);

/**
 * envoy_dynamic_module_callback_tracer_set_gauge is called by the module to set the value of
 * a previously defined gauge.
 *
 * @param config_envoy_ptr is the pointer to the DynamicModuleTracerConfig object.
 * @param id is the ID of the gauge previously defined using the config.
 * @param label_values is the values of the labels.
 * NOTE: label_values could be null if the label_values_length is 0.
 * @param label_values_length is the length of the label_values.
 * NOTE: label_values_length could be 0 if there are no labels. **THE LENGTH MUST MATCH THE
 * LABEL NAMES DEFINED DURING GAUGE DEFINITION.**
 * @param value is the value to set the gauge to.
 * @return the result of the operation.
 */
envoy_dynamic_module_type_metrics_result envoy_dynamic_module_callback_tracer_set_gauge(
    envoy_dynamic_module_type_tracer_config_envoy_ptr config_envoy_ptr, size_t id,
    envoy_dynamic_module_type_module_buffer* label_values, size_t label_values_length,
    uint64_t value);

// =============================================================================
// ================================ DNS Resolver ===============================
// =============================================================================

// =============================================================================
// DNS Resolver Types
// =============================================================================

/**
 * envoy_dynamic_module_type_dns_resolver_config_envoy_ptr is a raw pointer to the DNS resolver
 * configuration object in Envoy. This is passed to the module when creating a new in-module DNS
 * resolver configuration.
 *
 * This has 1:1 correspondence with envoy_dynamic_module_type_dns_resolver_config_module_ptr in the
 * module.
 *
 * OWNERSHIP: Envoy owns the pointer.
 */
typedef void* envoy_dynamic_module_type_dns_resolver_config_envoy_ptr;

/**
 * envoy_dynamic_module_type_dns_resolver_config_module_ptr is a pointer to an in-module DNS
 * resolver configuration object. This is created by the module via
 * envoy_dynamic_module_on_dns_resolver_config_new and passed back to the module in subsequent
 * calls.
 *
 * This has 1:1 correspondence with envoy_dynamic_module_type_dns_resolver_config_envoy_ptr in
 * Envoy.
 *
 * OWNERSHIP: The module is responsible for managing the lifetime of this pointer. Envoy will call
 * envoy_dynamic_module_on_dns_resolver_config_destroy when the configuration is no longer needed.
 */
typedef const void* envoy_dynamic_module_type_dns_resolver_config_module_ptr;

/**
 * envoy_dynamic_module_type_dns_resolver_module_ptr is a pointer to an in-module DNS resolver
 * instance. This is created by the module via envoy_dynamic_module_on_dns_resolver_new.
 *
 * OWNERSHIP: The module is responsible for managing the lifetime of this pointer. Envoy will call
 * envoy_dynamic_module_on_dns_resolver_destroy when the resolver is no longer needed.
 */
typedef const void* envoy_dynamic_module_type_dns_resolver_module_ptr;

/**
 * envoy_dynamic_module_type_dns_resolver_envoy_ptr is a pointer to the Envoy-side DNS resolver
 * instance. This is passed to the module so it can call back into Envoy (e.g., to deliver
 * resolution results via envoy_dynamic_module_callback_dns_resolve_complete).
 *
 * OWNERSHIP: Envoy owns this pointer. The module must not free it.
 */
typedef const void* envoy_dynamic_module_type_dns_resolver_envoy_ptr;

/**
 * envoy_dynamic_module_type_dns_query_module_ptr is a pointer to an in-module active DNS query
 * object. This is created by the module when envoy_dynamic_module_on_dns_resolve is called.
 *
 * OWNERSHIP: The module is responsible for managing the lifetime of this pointer. Envoy will call
 * envoy_dynamic_module_on_dns_resolve_cancel to cancel the query, after which the module should
 * clean it up.
 */
typedef const void* envoy_dynamic_module_type_dns_query_module_ptr;

/**
 * envoy_dynamic_module_type_dns_lookup_family specifies which address families to look up.
 * This corresponds to Network::DnsLookupFamily in Envoy.
 */
typedef enum envoy_dynamic_module_type_dns_lookup_family {
  envoy_dynamic_module_type_dns_lookup_family_V4Only,
  envoy_dynamic_module_type_dns_lookup_family_V6Only,
  envoy_dynamic_module_type_dns_lookup_family_Auto,
  envoy_dynamic_module_type_dns_lookup_family_V4Preferred,
  envoy_dynamic_module_type_dns_lookup_family_All,
} envoy_dynamic_module_type_dns_lookup_family;

/**
 * envoy_dynamic_module_type_dns_resolution_status represents the final status of a DNS resolution.
 * This corresponds to Network::DnsResolver::ResolutionStatus in Envoy.
 */
typedef enum envoy_dynamic_module_type_dns_resolution_status {
  envoy_dynamic_module_type_dns_resolution_status_Completed,
  envoy_dynamic_module_type_dns_resolution_status_Failure,
} envoy_dynamic_module_type_dns_resolution_status;

/**
 * envoy_dynamic_module_type_dns_address represents a single resolved DNS address with its TTL.
 * The address_ptr/address_length must contain an "ip:port" string (e.g., "1.2.3.4:0"). The port
 * must always be 0 because DNS resolution only produces IP addresses; the actual port comes from
 * the cluster/endpoint configuration. The ttl_seconds is the time-to-live in seconds for this
 * record.
 */
typedef struct envoy_dynamic_module_type_dns_address {
  envoy_dynamic_module_type_buffer_module_ptr address_ptr;
  size_t address_length;
  uint32_t ttl_seconds;
} envoy_dynamic_module_type_dns_address;

// =============================================================================
// DNS Resolver Event Hooks
// =============================================================================

/**
 * envoy_dynamic_module_on_dns_resolver_config_new is called by the main thread when a DNS resolver
 * configuration referencing this module is loaded. The module should parse the configuration and
 * return a pointer to the in-module configuration object.
 *
 * @param config_envoy_ptr is the pointer to the Envoy DNS resolver configuration object.
 * @param name is the resolver name identifying the implementation within the module.
 * @param config is the configuration bytes for the module.
 * @return envoy_dynamic_module_type_dns_resolver_config_module_ptr is the pointer to the in-module
 * DNS resolver configuration. Returning nullptr indicates a failure, and the configuration will be
 * rejected.
 */
envoy_dynamic_module_type_dns_resolver_config_module_ptr
envoy_dynamic_module_on_dns_resolver_config_new(
    envoy_dynamic_module_type_dns_resolver_config_envoy_ptr config_envoy_ptr,
    envoy_dynamic_module_type_envoy_buffer name, envoy_dynamic_module_type_envoy_buffer config);

/**
 * envoy_dynamic_module_on_dns_resolver_config_destroy is called when the DNS resolver configuration
 * is destroyed. The module should release any resources associated with the configuration.
 *
 * @param config_module_ptr is the pointer to the in-module DNS resolver configuration.
 */
void envoy_dynamic_module_on_dns_resolver_config_destroy(
    envoy_dynamic_module_type_dns_resolver_config_module_ptr config_module_ptr);

/**
 * envoy_dynamic_module_on_dns_resolver_new is called to create a new DNS resolver instance from
 * the given configuration.
 *
 * @param config_module_ptr is the pointer to the in-module DNS resolver configuration.
 * @param resolver_envoy_ptr is the Envoy-side resolver pointer, used by the module when calling
 * envoy_dynamic_module_callback_dns_resolve_complete.
 * @return envoy_dynamic_module_type_dns_resolver_module_ptr is the pointer to the in-module DNS
 * resolver instance. Returning nullptr indicates a failure to create the resolver.
 */
envoy_dynamic_module_type_dns_resolver_module_ptr envoy_dynamic_module_on_dns_resolver_new(
    envoy_dynamic_module_type_dns_resolver_config_module_ptr config_module_ptr,
    envoy_dynamic_module_type_dns_resolver_envoy_ptr resolver_envoy_ptr);

/**
 * envoy_dynamic_module_on_dns_resolver_destroy is called when the DNS resolver instance is
 * destroyed. The module should release the in-module resolver and shut down any background threads.
 *
 * @param resolver_module_ptr is the pointer to the in-module DNS resolver instance.
 */
void envoy_dynamic_module_on_dns_resolver_destroy(
    envoy_dynamic_module_type_dns_resolver_module_ptr resolver_module_ptr);

/**
 * envoy_dynamic_module_on_dns_resolve is called to initiate an asynchronous DNS resolution.
 * The module should start the resolution and return a query handle. When the resolution completes,
 * the module must call envoy_dynamic_module_callback_dns_resolve_complete (from any thread).
 *
 * @param resolver_module_ptr is the pointer to the in-module DNS resolver instance.
 * @param dns_name is the DNS name to resolve.
 * @param lookup_family is the address family to look up.
 * @param query_id is a unique identifier for this query, assigned by Envoy. The module must pass
 * this back in envoy_dynamic_module_callback_dns_resolve_complete.
 * @return envoy_dynamic_module_type_dns_query_module_ptr is the pointer to the in-module active
 * query. Returning nullptr indicates that the resolution could not be started.
 */
envoy_dynamic_module_type_dns_query_module_ptr envoy_dynamic_module_on_dns_resolve(
    envoy_dynamic_module_type_dns_resolver_module_ptr resolver_module_ptr,
    envoy_dynamic_module_type_envoy_buffer dns_name,
    envoy_dynamic_module_type_dns_lookup_family lookup_family, uint64_t query_id);

/**
 * envoy_dynamic_module_on_dns_resolve_cancel is called to cancel an in-flight DNS query. After
 * this call, the module must not call envoy_dynamic_module_callback_dns_resolve_complete for the
 * cancelled query. The module should clean up any resources associated with the query.
 *
 * @param resolver_module_ptr is the pointer to the in-module DNS resolver instance.
 * @param query_module_ptr is the pointer to the in-module active query returned by
 * envoy_dynamic_module_on_dns_resolve.
 */
void envoy_dynamic_module_on_dns_resolve_cancel(
    envoy_dynamic_module_type_dns_resolver_module_ptr resolver_module_ptr,
    envoy_dynamic_module_type_dns_query_module_ptr query_module_ptr);

/**
 * envoy_dynamic_module_on_dns_resolver_reset_networking is called to reset the resolver's
 * networking state, typically in response to a network change (e.g., WiFi to cellular).
 * The module may recreate connections, re-read system configuration, etc.
 *
 * @param resolver_module_ptr is the pointer to the in-module DNS resolver instance.
 */
void envoy_dynamic_module_on_dns_resolver_reset_networking(
    envoy_dynamic_module_type_dns_resolver_module_ptr resolver_module_ptr);

// =============================================================================
// DNS Resolver Callbacks
// =============================================================================

/**
 * envoy_dynamic_module_callback_dns_resolve_complete is called by the module to deliver DNS
 * resolution results back to Envoy.
 *
 * THREAD SAFETY: This function is safe to call from any thread. The C++ shell will post the
 * results to the correct Envoy dispatcher thread.
 *
 * BUFFER LIFETIME: All buffer data (details, address strings) is copied synchronously before this
 * function returns. The caller only needs to keep the data valid for the duration of the call.
 *
 * @param resolver_envoy_ptr is the Envoy-side resolver pointer passed during resolver creation.
 * @param query_id is the query identifier that was passed to envoy_dynamic_module_on_dns_resolve.
 * @param status is the resolution status (Completed or Failure).
 * @param details is a human-readable string describing the resolution result.
 * @param addresses is an array of resolved addresses with TTLs.
 * @param num_addresses is the number of elements in the addresses array.
 */
void envoy_dynamic_module_callback_dns_resolve_complete(
    envoy_dynamic_module_type_dns_resolver_envoy_ptr resolver_envoy_ptr, uint64_t query_id,
    envoy_dynamic_module_type_dns_resolution_status status,
    envoy_dynamic_module_type_module_buffer details,
    const envoy_dynamic_module_type_dns_address* addresses, size_t num_addresses);

// =============================================================================
// DNS Resolver Callbacks - Metrics
// =============================================================================

/**
 * envoy_dynamic_module_callback_dns_resolver_config_define_counter is called by the module during
 * initialization to create a template for generating Stats::Counters with the given name and
 * labels during the lifecycle of the module.
 *
 * @param config_envoy_ptr is the pointer to the DNS resolver configuration in which the counter
 * will be defined.
 * @param name is the name of the counter to be defined.
 * @param label_names is the labels of the counter to be defined.
 * NOTE: label names could be null if the label_names_length is 0.
 * @param label_names_length is the length of the label_names.
 * NOTE: label_names_length could be 0 if there are no labels.
 * @param counter_id_ptr where the opaque ID that represents a unique metric will be stored. This
 * can be passed to envoy_dynamic_module_callback_dns_resolver_config_increment_counter together
 * with config_envoy_ptr.
 * @return the result of the operation.
 */
envoy_dynamic_module_type_metrics_result
envoy_dynamic_module_callback_dns_resolver_config_define_counter(
    envoy_dynamic_module_type_dns_resolver_config_envoy_ptr config_envoy_ptr,
    envoy_dynamic_module_type_module_buffer name,
    envoy_dynamic_module_type_module_buffer* label_names, size_t label_names_length,
    size_t* counter_id_ptr);

/**
 * envoy_dynamic_module_callback_dns_resolver_config_increment_counter is called by the module to
 * increment a previously defined counter.
 *
 * @param config_envoy_ptr is the pointer to the DNS resolver configuration.
 * @param id is the ID of the counter previously defined using the config.
 * @param label_values is the values of the labels to be incremented.
 * NOTE: label_values could be null if the label_values_length is 0.
 * @param label_values_length is the length of the label_values.
 * NOTE: label_values_length could be 0 if there are no labels. **THE LENGTH MUST MATCH THE
 * LABEL NAMES DEFINED DURING COUNTER DEFINITION.**
 * @param value is the value to increment the counter by.
 * @return the result of the operation.
 */
envoy_dynamic_module_type_metrics_result
envoy_dynamic_module_callback_dns_resolver_config_increment_counter(
    envoy_dynamic_module_type_dns_resolver_config_envoy_ptr config_envoy_ptr, size_t id,
    envoy_dynamic_module_type_module_buffer* label_values, size_t label_values_length,
    uint64_t value);

/**
 * envoy_dynamic_module_callback_dns_resolver_config_define_gauge is called by the module during
 * initialization to create a template for generating Stats::Gauges with the given name and
 * labels during the lifecycle of the module.
 *
 * @param config_envoy_ptr is the pointer to the DNS resolver configuration in which the gauge
 * will be defined.
 * @param name is the name of the gauge to be defined.
 * @param label_names is the labels of the gauge to be defined.
 * NOTE: label names could be null if the label_names_length is 0.
 * @param label_names_length is the length of the label_names.
 * NOTE: label_names_length could be 0 if there are no labels.
 * @param gauge_id_ptr where the opaque ID that represents a unique metric will be stored.
 * @return the result of the operation.
 */
envoy_dynamic_module_type_metrics_result
envoy_dynamic_module_callback_dns_resolver_config_define_gauge(
    envoy_dynamic_module_type_dns_resolver_config_envoy_ptr config_envoy_ptr,
    envoy_dynamic_module_type_module_buffer name,
    envoy_dynamic_module_type_module_buffer* label_names, size_t label_names_length,
    size_t* gauge_id_ptr);

/**
 * envoy_dynamic_module_callback_dns_resolver_config_set_gauge is called by the module to set the
 * value of a previously defined gauge.
 *
 * @param config_envoy_ptr is the pointer to the DNS resolver configuration.
 * @param id is the ID of the gauge previously defined using the config.
 * @param label_values is the values of the labels.
 * NOTE: label_values could be null if the label_values_length is 0.
 * @param label_values_length is the length of the label_values.
 * NOTE: label_values_length could be 0 if there are no labels. **THE LENGTH MUST MATCH THE
 * LABEL NAMES DEFINED DURING GAUGE DEFINITION.**
 * @param value is the value to set the gauge to.
 * @return the result of the operation.
 */
envoy_dynamic_module_type_metrics_result
envoy_dynamic_module_callback_dns_resolver_config_set_gauge(
    envoy_dynamic_module_type_dns_resolver_config_envoy_ptr config_envoy_ptr, size_t id,
    envoy_dynamic_module_type_module_buffer* label_values, size_t label_values_length,
    uint64_t value);

/**
 * envoy_dynamic_module_callback_dns_resolver_config_increment_gauge is called by the module to
 * increment a previously defined gauge.
 *
 * @param config_envoy_ptr is the pointer to the DNS resolver configuration.
 * @param id is the ID of the gauge previously defined using the config.
 * @param label_values is the values of the labels.
 * NOTE: label_values could be null if the label_values_length is 0.
 * @param label_values_length is the length of the label_values.
 * NOTE: label_values_length could be 0 if there are no labels. **THE LENGTH MUST MATCH THE
 * LABEL NAMES DEFINED DURING GAUGE DEFINITION.**
 * @param value is the value to increment the gauge by.
 * @return the result of the operation.
 */
envoy_dynamic_module_type_metrics_result
envoy_dynamic_module_callback_dns_resolver_config_increment_gauge(
    envoy_dynamic_module_type_dns_resolver_config_envoy_ptr config_envoy_ptr, size_t id,
    envoy_dynamic_module_type_module_buffer* label_values, size_t label_values_length,
    uint64_t value);

/**
 * envoy_dynamic_module_callback_dns_resolver_config_decrement_gauge is called by the module to
 * decrement a previously defined gauge.
 *
 * @param config_envoy_ptr is the pointer to the DNS resolver configuration.
 * @param id is the ID of the gauge previously defined using the config.
 * @param label_values is the values of the labels.
 * NOTE: label_values could be null if the label_values_length is 0.
 * @param label_values_length is the length of the label_values.
 * NOTE: label_values_length could be 0 if there are no labels. **THE LENGTH MUST MATCH THE
 * LABEL NAMES DEFINED DURING GAUGE DEFINITION.**
 * @param value is the value to decrement the gauge by.
 * @return the result of the operation.
 */
envoy_dynamic_module_type_metrics_result
envoy_dynamic_module_callback_dns_resolver_config_decrement_gauge(
    envoy_dynamic_module_type_dns_resolver_config_envoy_ptr config_envoy_ptr, size_t id,
    envoy_dynamic_module_type_module_buffer* label_values, size_t label_values_length,
    uint64_t value);

/**
 * envoy_dynamic_module_callback_dns_resolver_config_define_histogram is called by the module during
 * initialization to create a template for generating Stats::Histograms with the given name and
 * labels during the lifecycle of the module.
 *
 * @param config_envoy_ptr is the pointer to the DNS resolver configuration in which the histogram
 * will be defined.
 * @param name is the name of the histogram to be defined.
 * @param label_names is the labels of the histogram to be defined.
 * NOTE: label names could be null if the label_names_length is 0.
 * @param label_names_length is the length of the label_names.
 * NOTE: label_names_length could be 0 if there are no labels.
 * @param histogram_id_ptr where the opaque ID that represents a unique metric will be stored.
 * @return the result of the operation.
 */
envoy_dynamic_module_type_metrics_result
envoy_dynamic_module_callback_dns_resolver_config_define_histogram(
    envoy_dynamic_module_type_dns_resolver_config_envoy_ptr config_envoy_ptr,
    envoy_dynamic_module_type_module_buffer name,
    envoy_dynamic_module_type_module_buffer* label_names, size_t label_names_length,
    size_t* histogram_id_ptr);

/**
 * envoy_dynamic_module_callback_dns_resolver_config_record_histogram_value is called by the module
 * to record a value for a previously defined histogram.
 *
 * @param config_envoy_ptr is the pointer to the DNS resolver configuration.
 * @param id is the ID of the histogram previously defined using the config.
 * @param label_values is the values of the labels.
 * NOTE: label_values could be null if the label_values_length is 0.
 * @param label_values_length is the length of the label_values.
 * NOTE: label_values_length could be 0 if there are no labels. **THE LENGTH MUST MATCH THE
 * LABEL NAMES DEFINED DURING HISTOGRAM DEFINITION.**
 * @param value is the value to record.
 * @return the result of the operation.
 */
envoy_dynamic_module_type_metrics_result
envoy_dynamic_module_callback_dns_resolver_config_record_histogram_value(
    envoy_dynamic_module_type_dns_resolver_config_envoy_ptr config_envoy_ptr, size_t id,
    envoy_dynamic_module_type_module_buffer* label_values, size_t label_values_length,
    uint64_t value);

// =============================================================================
// =========================== Transport Socket ================================
// =============================================================================

// =============================================================================
// Transport Socket Types
// =============================================================================

/**
 * envoy_dynamic_module_type_transport_socket_factory_config_envoy_ptr is a raw pointer to the
 * transport socket factory configuration object in Envoy. This is passed to the module when
 * creating a new in-module transport socket factory configuration.
 *
 * This has 1:1 correspondence with
 * envoy_dynamic_module_type_transport_socket_factory_config_module_ptr in the module.
 *
 * OWNERSHIP: Envoy owns the pointer.
 */
typedef void* envoy_dynamic_module_type_transport_socket_factory_config_envoy_ptr;

/**
 * envoy_dynamic_module_type_transport_socket_factory_config_module_ptr is a pointer to an in-module
 * transport socket factory configuration object. This is created by the module via
 * envoy_dynamic_module_on_transport_socket_factory_config_new and passed back to the module in
 * subsequent calls.
 *
 * This has 1:1 correspondence with
 * envoy_dynamic_module_type_transport_socket_factory_config_envoy_ptr in Envoy.
 *
 * OWNERSHIP: The module is responsible for managing the lifetime of this pointer. Envoy will call
 * envoy_dynamic_module_on_transport_socket_factory_config_destroy when the configuration is no
 * longer needed.
 */
typedef const void* envoy_dynamic_module_type_transport_socket_factory_config_module_ptr;

/**
 * envoy_dynamic_module_type_transport_socket_envoy_ptr is a raw pointer to the transport socket
 * object in Envoy. This is passed to the module when a new transport socket is created for a
 * connection and is used to call back into Envoy (e.g., buffer and I/O handle operations).
 *
 * This has 1:1 correspondence with envoy_dynamic_module_type_transport_socket_module_ptr in the
 * module.
 *
 * OWNERSHIP: Envoy owns the pointer. The module must not free it. It remains valid until
 * envoy_dynamic_module_on_transport_socket_destroy is called for the corresponding in-module
 * transport socket.
 */
typedef void* envoy_dynamic_module_type_transport_socket_envoy_ptr;

/**
 * envoy_dynamic_module_type_transport_socket_module_ptr is a pointer to an in-module transport
 * socket instance. This is created by the module via envoy_dynamic_module_on_transport_socket_new.
 *
 * OWNERSHIP: The module is responsible for managing the lifetime of this pointer. Envoy will call
 * envoy_dynamic_module_on_transport_socket_destroy when the transport socket is no longer needed.
 */
typedef const void* envoy_dynamic_module_type_transport_socket_module_ptr;

/**
 * envoy_dynamic_module_type_transport_socket_post_io_action specifies what should happen on the
 * connection after an I/O operation. This corresponds to Network::PostIoAction in Envoy.
 */
typedef enum envoy_dynamic_module_type_transport_socket_post_io_action {
  envoy_dynamic_module_type_transport_socket_post_io_action_KeepOpen,
  envoy_dynamic_module_type_transport_socket_post_io_action_Close,
} envoy_dynamic_module_type_transport_socket_post_io_action;

/**
 * envoy_dynamic_module_type_transport_socket_io_result is the result of a transport socket read or
 * write operation. This corresponds to Network::IoResult in Envoy.
 */
typedef struct envoy_dynamic_module_type_transport_socket_io_result {
  envoy_dynamic_module_type_transport_socket_post_io_action action;
  uint64_t bytes_processed;
  bool end_stream_read;
} envoy_dynamic_module_type_transport_socket_io_result;

// =============================================================================
// Transport Socket Event Hooks
// =============================================================================

/**
 * envoy_dynamic_module_on_transport_socket_factory_config_new is called by the main thread when a
 * transport socket factory configuration referencing this module is loaded. The module should parse
 * the configuration and return a pointer to the in-module factory configuration object.
 *
 * @param factory_config_envoy_ptr is the pointer to the Envoy transport socket factory
 * configuration object.
 * @param socket_name is the name identifying the transport socket implementation within the module.
 * @param socket_config is the configuration bytes for the module.
 * @param is_upstream is true if this factory is for upstream connections, false for downstream.
 * @return envoy_dynamic_module_type_transport_socket_factory_config_module_ptr is the pointer to
 * the in-module factory configuration. Returning nullptr indicates a failure, and the configuration
 * will be rejected.
 */
envoy_dynamic_module_type_transport_socket_factory_config_module_ptr
envoy_dynamic_module_on_transport_socket_factory_config_new(
    envoy_dynamic_module_type_transport_socket_factory_config_envoy_ptr factory_config_envoy_ptr,
    envoy_dynamic_module_type_envoy_buffer socket_name,
    envoy_dynamic_module_type_envoy_buffer socket_config, bool is_upstream);

/**
 * envoy_dynamic_module_on_transport_socket_factory_config_destroy is called when the transport
 * socket factory configuration is destroyed. The module should release any resources associated
 * with the configuration.
 *
 * @param factory_config_ptr is the pointer to the in-module transport socket factory
 * configuration.
 */
void envoy_dynamic_module_on_transport_socket_factory_config_destroy(
    envoy_dynamic_module_type_transport_socket_factory_config_module_ptr factory_config_ptr);

/**
 * envoy_dynamic_module_on_transport_socket_new is called when a new transport socket is created
 * for a connection.
 *
 * @param factory_config_ptr is the pointer to the in-module transport socket factory
 * configuration.
 * @param transport_socket_envoy_ptr is the Envoy-side transport socket pointer, used by the module
 * when calling transport socket callbacks.
 * @return envoy_dynamic_module_type_transport_socket_module_ptr is the pointer to the in-module
 * transport socket. Returning nullptr indicates a failure to create the transport socket, and the
 * connection will be closed.
 */
envoy_dynamic_module_type_transport_socket_module_ptr envoy_dynamic_module_on_transport_socket_new(
    envoy_dynamic_module_type_transport_socket_factory_config_module_ptr factory_config_ptr,
    envoy_dynamic_module_type_transport_socket_envoy_ptr transport_socket_envoy_ptr);

/**
 * envoy_dynamic_module_on_transport_socket_destroy is called when the transport socket is
 * destroyed. The module should release the in-module transport socket and any associated
 * resources.
 *
 * @param transport_socket_module_ptr is the pointer to the in-module transport socket.
 */
void envoy_dynamic_module_on_transport_socket_destroy(
    envoy_dynamic_module_type_transport_socket_module_ptr transport_socket_module_ptr);

/**
 * envoy_dynamic_module_on_transport_socket_set_callbacks is called once to supply the transport
 * socket with its Envoy callbacks before any I/O operations occur.
 *
 * @param transport_socket_envoy_ptr is the pointer to the Envoy transport socket object.
 * @param transport_socket_module_ptr is the pointer to the in-module transport socket.
 */
void envoy_dynamic_module_on_transport_socket_set_callbacks(
    envoy_dynamic_module_type_transport_socket_envoy_ptr transport_socket_envoy_ptr,
    envoy_dynamic_module_type_transport_socket_module_ptr transport_socket_module_ptr);

/**
 * envoy_dynamic_module_on_transport_socket_on_connected is called when the underlying transport is
 * established.
 *
 * @param transport_socket_envoy_ptr is the pointer to the Envoy transport socket object.
 * @param transport_socket_module_ptr is the pointer to the in-module transport socket.
 */
void envoy_dynamic_module_on_transport_socket_on_connected(
    envoy_dynamic_module_type_transport_socket_envoy_ptr transport_socket_envoy_ptr,
    envoy_dynamic_module_type_transport_socket_module_ptr transport_socket_module_ptr);

/**
 * envoy_dynamic_module_on_transport_socket_do_read is called when data is to be read from the
 * connection and decrypted or transformed into the read buffer.
 *
 * @param transport_socket_envoy_ptr is the pointer to the Envoy transport socket object.
 * @param transport_socket_module_ptr is the pointer to the in-module transport socket.
 * @return envoy_dynamic_module_type_transport_socket_io_result is the result of the read operation.
 */
envoy_dynamic_module_type_transport_socket_io_result
envoy_dynamic_module_on_transport_socket_do_read(
    envoy_dynamic_module_type_transport_socket_envoy_ptr transport_socket_envoy_ptr,
    envoy_dynamic_module_type_transport_socket_module_ptr transport_socket_module_ptr);

/**
 * envoy_dynamic_module_on_transport_socket_do_write is called when data is to be written to the
 * connection from the write buffer.
 *
 * @param transport_socket_envoy_ptr is the pointer to the Envoy transport socket object.
 * @param transport_socket_module_ptr is the pointer to the in-module transport socket.
 * @param write_buffer_length is the length of the write buffer at the time of the call.
 * @param end_stream is true if this is the end of the stream (half-close after a full write).
 * @return envoy_dynamic_module_type_transport_socket_io_result is the result of the write
 * operation.
 */
envoy_dynamic_module_type_transport_socket_io_result
envoy_dynamic_module_on_transport_socket_do_write(
    envoy_dynamic_module_type_transport_socket_envoy_ptr transport_socket_envoy_ptr,
    envoy_dynamic_module_type_transport_socket_module_ptr transport_socket_module_ptr,
    size_t write_buffer_length, bool end_stream);

/**
 * envoy_dynamic_module_on_transport_socket_close is called when the transport socket is being
 * closed.
 *
 * @param transport_socket_envoy_ptr is the pointer to the Envoy transport socket object.
 * @param transport_socket_module_ptr is the pointer to the in-module transport socket.
 * @param event is the connection event that caused the close.
 */
void envoy_dynamic_module_on_transport_socket_close(
    envoy_dynamic_module_type_transport_socket_envoy_ptr transport_socket_envoy_ptr,
    envoy_dynamic_module_type_transport_socket_module_ptr transport_socket_module_ptr,
    envoy_dynamic_module_type_network_connection_event event);

/**
 * envoy_dynamic_module_on_transport_socket_get_protocol is called to obtain the negotiated
 * application-level protocol (e.g., ALPN), if any.
 *
 * @param transport_socket_envoy_ptr is the pointer to the Envoy transport socket object.
 * @param transport_socket_module_ptr is the pointer to the in-module transport socket.
 * @param result is filled by the module with the protocol string as an
 * envoy_dynamic_module_type_module_buffer. An empty buffer means no protocol was negotiated.
 */
void envoy_dynamic_module_on_transport_socket_get_protocol(
    envoy_dynamic_module_type_transport_socket_envoy_ptr transport_socket_envoy_ptr,
    envoy_dynamic_module_type_transport_socket_module_ptr transport_socket_module_ptr,
    envoy_dynamic_module_type_module_buffer* result);

/**
 * envoy_dynamic_module_on_transport_socket_get_failure_reason is called to obtain a description
 * of the last failure on the transport socket, if any.
 *
 * @param transport_socket_envoy_ptr is the pointer to the Envoy transport socket object.
 * @param transport_socket_module_ptr is the pointer to the in-module transport socket.
 * @param result is filled by the module with the failure reason as an
 * envoy_dynamic_module_type_module_buffer. An empty buffer means there is no failure reason.
 */
void envoy_dynamic_module_on_transport_socket_get_failure_reason(
    envoy_dynamic_module_type_transport_socket_envoy_ptr transport_socket_envoy_ptr,
    envoy_dynamic_module_type_transport_socket_module_ptr transport_socket_module_ptr,
    envoy_dynamic_module_type_module_buffer* result);

/**
 * envoy_dynamic_module_on_transport_socket_can_flush_close is called to determine whether the
 * socket may be flushed and closed.
 *
 * @param transport_socket_envoy_ptr is the pointer to the Envoy transport socket object.
 * @param transport_socket_module_ptr is the pointer to the in-module transport socket.
 * @return true if the socket can be flushed and closed, false otherwise.
 */
bool envoy_dynamic_module_on_transport_socket_can_flush_close(
    envoy_dynamic_module_type_transport_socket_envoy_ptr transport_socket_envoy_ptr,
    envoy_dynamic_module_type_transport_socket_module_ptr transport_socket_module_ptr);

// =============================================================================
// Transport Socket Callbacks
// =============================================================================

/**
 * envoy_dynamic_module_callback_transport_socket_get_io_handle returns an opaque pointer to the
 * underlying I/O handle for raw socket operations.
 *
 * @param transport_socket_envoy_ptr is the pointer to the Envoy transport socket object.
 * @return an opaque I/O handle pointer for use with
 * envoy_dynamic_module_callback_transport_socket_io_handle_read and
 * envoy_dynamic_module_callback_transport_socket_io_handle_write.
 */
void* envoy_dynamic_module_callback_transport_socket_get_io_handle(
    envoy_dynamic_module_type_transport_socket_envoy_ptr transport_socket_envoy_ptr);

/**
 * envoy_dynamic_module_callback_transport_socket_io_handle_read reads data from the raw socket
 * into the supplied buffer.
 *
 * @param io_handle is the opaque handle returned by
 * envoy_dynamic_module_callback_transport_socket_get_io_handle.
 * @param buffer is the buffer to read into.
 * @param length is the maximum number of bytes to read.
 * @param bytes_read is set to the number of bytes actually read. Must not be null.
 * @return 0 on success, or a negative system errno value on failure (e.g., -EAGAIN).
 */
int64_t envoy_dynamic_module_callback_transport_socket_io_handle_read(void* io_handle, char* buffer,
                                                                      size_t length,
                                                                      size_t* bytes_read);

/**
 * envoy_dynamic_module_callback_transport_socket_io_handle_write writes data to the raw socket
 * from the supplied buffer.
 *
 * @param io_handle is the opaque handle returned by
 * envoy_dynamic_module_callback_transport_socket_get_io_handle.
 * @param buffer is the buffer to write from.
 * @param length is the number of bytes to write.
 * @param bytes_written is set to the number of bytes actually written. Must not be null.
 * @return 0 on success, or a negative system errno value on failure (e.g., -EAGAIN).
 */
int64_t envoy_dynamic_module_callback_transport_socket_io_handle_write(void* io_handle,
                                                                       const char* buffer,
                                                                       size_t length,
                                                                       size_t* bytes_written);

/**
 * envoy_dynamic_module_callback_transport_socket_io_handle_fd returns the native OS file descriptor
 * for the I/O handle, or -1 if the handle does not wrap a native socket.
 *
 * @param io_handle is the opaque handle returned by
 * envoy_dynamic_module_callback_transport_socket_get_io_handle.
 * @return the native file descriptor, or -1 if unavailable.
 */
int envoy_dynamic_module_callback_transport_socket_io_handle_fd(void* io_handle);

/**
 * envoy_dynamic_module_callback_transport_socket_read_buffer_drain drains bytes from the beginning
 * of the connection read buffer.
 *
 * @param transport_socket_envoy_ptr is the pointer to the Envoy transport socket object.
 * @param length is the number of bytes to drain.
 */
void envoy_dynamic_module_callback_transport_socket_read_buffer_drain(
    envoy_dynamic_module_type_transport_socket_envoy_ptr transport_socket_envoy_ptr, size_t length);

/**
 * envoy_dynamic_module_callback_transport_socket_read_buffer_add appends data to the connection
 * read buffer.
 *
 * @param transport_socket_envoy_ptr is the pointer to the Envoy transport socket object.
 * @param data is a pointer to the data to add.
 * @param length is the length of the data in bytes.
 */
void envoy_dynamic_module_callback_transport_socket_read_buffer_add(
    envoy_dynamic_module_type_transport_socket_envoy_ptr transport_socket_envoy_ptr,
    const char* data, size_t length);

/**
 * envoy_dynamic_module_callback_transport_socket_read_buffer_length returns the current length of
 * the connection read buffer.
 *
 * @param transport_socket_envoy_ptr is the pointer to the Envoy transport socket object.
 * @return the length of the read buffer in bytes.
 */
size_t envoy_dynamic_module_callback_transport_socket_read_buffer_length(
    envoy_dynamic_module_type_transport_socket_envoy_ptr transport_socket_envoy_ptr);

/**
 * envoy_dynamic_module_callback_transport_socket_write_buffer_drain drains bytes from the beginning
 * of the connection write buffer.
 *
 * @param transport_socket_envoy_ptr is the pointer to the Envoy transport socket object.
 * @param length is the number of bytes to drain.
 */
void envoy_dynamic_module_callback_transport_socket_write_buffer_drain(
    envoy_dynamic_module_type_transport_socket_envoy_ptr transport_socket_envoy_ptr, size_t length);

/**
 * envoy_dynamic_module_callback_transport_socket_write_buffer_get_slices copies or queries write
 * buffer slices. If slices is NULL, the function runs in query mode: slices_count is set to the
 * total number of slices available. Otherwise, up to slices_count slices are written into slices,
 * and slices_count is set to the number of slices returned.
 *
 * @param transport_socket_envoy_ptr is the pointer to the Envoy transport socket object.
 * @param slices is the output array of envoy_dynamic_module_type_envoy_buffer, or NULL for query
 * mode.
 * @param slices_count is the maximum number of slices to return on input, and the actual count on
 * output.
 */
void envoy_dynamic_module_callback_transport_socket_write_buffer_get_slices(
    envoy_dynamic_module_type_transport_socket_envoy_ptr transport_socket_envoy_ptr,
    envoy_dynamic_module_type_envoy_buffer* slices, size_t* slices_count);

/**
 * envoy_dynamic_module_callback_transport_socket_write_buffer_length returns the current length of
 * the connection write buffer.
 *
 * @param transport_socket_envoy_ptr is the pointer to the Envoy transport socket object.
 * @return the length of the write buffer in bytes.
 */
size_t envoy_dynamic_module_callback_transport_socket_write_buffer_length(
    envoy_dynamic_module_type_transport_socket_envoy_ptr transport_socket_envoy_ptr);

/**
 * envoy_dynamic_module_callback_transport_socket_raise_event raises a connection event on the
 * connection (e.g., Connected after TLS handshake).
 *
 * @param transport_socket_envoy_ptr is the pointer to the Envoy transport socket object.
 * @param event is the connection event to raise.
 */
void envoy_dynamic_module_callback_transport_socket_raise_event(
    envoy_dynamic_module_type_transport_socket_envoy_ptr transport_socket_envoy_ptr,
    envoy_dynamic_module_type_network_connection_event event);

/**
 * envoy_dynamic_module_callback_transport_socket_should_drain_read_buffer returns whether the read
 * buffer should be drained to enforce read limits and yielding.
 *
 * @param transport_socket_envoy_ptr is the pointer to the Envoy transport socket object.
 * @return true if the read buffer should be drained, false otherwise.
 */
bool envoy_dynamic_module_callback_transport_socket_should_drain_read_buffer(
    envoy_dynamic_module_type_transport_socket_envoy_ptr transport_socket_envoy_ptr);

/**
 * envoy_dynamic_module_callback_transport_socket_set_is_readable marks the transport socket as
 * readable so that a read will be scheduled on a future event loop iteration.
 *
 * @param transport_socket_envoy_ptr is the pointer to the Envoy transport socket object.
 */
void envoy_dynamic_module_callback_transport_socket_set_is_readable(
    envoy_dynamic_module_type_transport_socket_envoy_ptr transport_socket_envoy_ptr);

/**
 * envoy_dynamic_module_callback_transport_socket_flush_write_buffer attempts to drain a non-empty
 * write buffer to the underlying transport.
 *
 * @param transport_socket_envoy_ptr is the pointer to the Envoy transport socket object.
 */
void envoy_dynamic_module_callback_transport_socket_flush_write_buffer(
    envoy_dynamic_module_type_transport_socket_envoy_ptr transport_socket_envoy_ptr);

#ifdef __cplusplus
}
#endif

// NOLINTEND
