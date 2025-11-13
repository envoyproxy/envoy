#pragma once

// NOLINT(namespace-envoy)

// This is a pure C header, so we can't apply clang-tidy to it.
// NOLINTBEGIN

// This is a pure C header file that defines the ABI of the core of dynamic modules used by Envoy.
//
// This must not contain any dependencies besides standard library since it is not only used by
// Envoy itself but also by dynamic module SDKs written in non-C++ languages.
//
// Currently, compatibility is only guaranteed by an exact version match between the Envoy
// codebase and the dynamic module SDKs. In the future, after the ABI is stabilized, we will revisit
// this restriction and hopefully provide a wider compatibility guarantee. Until then, Envoy
// checks the hash of the ABI header files to ensure that the dynamic modules are built against the
// same version of the ABI.
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

#ifdef __cplusplus
#include <cstdbool>
#include <cstddef>
#include <cstdint>

extern "C" {
#else

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#endif

// -----------------------------------------------------------------------------
// ---------------------------------- Types ------------------------------------
// -----------------------------------------------------------------------------
//
// Types used in the ABI. The name of a type must be prefixed with "envoy_dynamic_module_type_".
// Types with "_module_ptr" suffix are pointers owned by the module, i.e. memory space allocated by
// the module. Types with "_envoy_ptr" suffix are pointers owned by Envoy, i.e. memory space
// allocated by Envoy.

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
 * envoy_dynamic_module_type_buffer_module_ptr is a pointer to a buffer in the module. A buffer
 * represents a contiguous block of memory in bytes.
 *
 * OWNERSHIP: The module is responsible for managing the lifetime of the pointer. It depends on the
 * context where the buffer is used. See for the specific event hook or callback for more details.
 */
typedef char* envoy_dynamic_module_type_buffer_module_ptr;

/**
 * envoy_dynamic_module_type_buffer_module_ptr is a pointer to a buffer in the module. A buffer
 * represents a contiguous block of memory in bytes.
 * The buffer is read-only.
 *
 * OWNERSHIP: The module is responsible for managing the lifetime of the pointer. It depends on the
 * context where the buffer is used. See for the specific event hook or callback for more details.
 */
typedef const char* envoy_dynamic_module_type_const_buffer_module_ptr;

/**
 * envoy_dynamic_module_type_buffer_envoy_ptr is a pointer to a buffer in Envoy. A buffer represents
 * a contiguous block of memory in bytes.
 *
 * OWNERSHIP: Envoy owns the pointer. The lifetime depends on the context where the buffer is used.
 * See for the specific event hook or callback for more details.
 */
typedef char* envoy_dynamic_module_type_buffer_envoy_ptr;

/**
 * envoy_dynamic_module_type_buffer_envoy_ptr is a pointer to a buffer in Envoy. A buffer represents
 * a contiguous block of memory in bytes.
 * The buffer is read-only.
 *
 * OWNERSHIP: Envoy owns the pointer. The lifetime depends on the context where the buffer is used.
 * See for the specific event hook or callback for more details.
 */
typedef const char* envoy_dynamic_module_type_const_buffer_envoy_ptr;

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
} envoy_dynamic_module_type_attribute_id;

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

// -----------------------------------------------------------------------------
// ------------------------------- Event Hooks ---------------------------------
// -----------------------------------------------------------------------------
//
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

/**
 * envoy_dynamic_module_on_http_filter_config_new is called by the main thread when the http
 * filter config is loaded. The function returns a
 * envoy_dynamic_module_type_http_filter_config_module_ptr for given name and config.
 *
 * @param filter_config_envoy_ptr is the pointer to the DynamicModuleHttpFilterConfig object for the
 * corresponding config.
 * @param name_ptr is the name of the filter.
 * @param name_size is the size of the name.
 * @param config_ptr is the configuration for the module.
 * @param config_size is the size of the configuration.
 * @return envoy_dynamic_module_type_http_filter_config_module_ptr is the pointer to the
 * in-module HTTP filter configuration. Returning nullptr indicates a failure to initialize the
 * module. When it fails, the filter configuration will be rejected.
 */
envoy_dynamic_module_type_http_filter_config_module_ptr
envoy_dynamic_module_on_http_filter_config_new(
    envoy_dynamic_module_type_http_filter_config_envoy_ptr filter_config_envoy_ptr,
    envoy_dynamic_module_type_const_buffer_envoy_ptr name_ptr, size_t name_size,
    envoy_dynamic_module_type_const_buffer_envoy_ptr config_ptr, size_t config_size);

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
 * @param name_ptr is the name of the filter.
 * @param name_size is the size of the name.
 * @param config_ptr is the configuration for the module.
 * @param config_size is the size of the configuration.
 * @return envoy_dynamic_module_type_http_filter_per_route_config_module_ptr is the pointer to the
 * in-module HTTP filter configuration. Returning nullptr indicates a failure to initialize the
 * module. When it fails, the filter configuration will be rejected.
 */
envoy_dynamic_module_type_http_filter_per_route_config_module_ptr
envoy_dynamic_module_on_http_filter_per_route_config_new(
    envoy_dynamic_module_type_const_buffer_envoy_ptr name_ptr, size_t name_size,
    envoy_dynamic_module_type_const_buffer_envoy_ptr config_ptr, size_t config_size);

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
    envoy_dynamic_module_type_http_filter_module_ptr filter_module_ptr, uint32_t callout_id,
    envoy_dynamic_module_type_http_callout_result result,
    envoy_dynamic_module_type_envoy_http_header* headers, size_t headers_size,
    envoy_dynamic_module_type_envoy_buffer* body_chunks, size_t body_chunks_size);

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

// -----------------------------------------------------------------------------
// -------------------------------- Callbacks ----------------------------------
// -----------------------------------------------------------------------------
//
// Callbacks are functions implemented by Envoy that can be called by the module to interact with
// Envoy. The name of a callback must be prefixed with "envoy_dynamic_module_callback_".

// --------------------------------- Logging -----------------------------------

/**
 * envoy_dynamic_module_callback_log is called by the module to log a message as part
 * of the standard Envoy logging stream under [dynamic_modules] Id.
 *
 * @param level is the log level of the message.
 * @param message_ptr is the pointer to the message to be logged.
 * @param message_length is the length of the message.
 *
 */
void envoy_dynamic_module_callback_log(
    envoy_dynamic_module_type_log_level level,
    envoy_dynamic_module_type_const_buffer_module_ptr message_ptr, size_t message_length);

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

// ----------------------------- Metrics callbacks -----------------------------

/**
 * envoy_dynamic_module_callback_http_filter_config_define_counter is called by the module during
 * initialization to create a new Stats::Counter with the given name.
 *
 * @param filter_config_envoy_ptr is the pointer to the DynamicModuleHttpFilterConfig in which the
 * counter will be defined.
 * @param name is the name of the counter to be defined.
 * @param name_length is the length of the name.
 * @param counter_id_ptr where the opaque ID that represents a unique metric will be stored. This
 * can be passed to envoy_dynamic_module_callback_http_filter_increment_counter together with
 * filter_envoy_ptr created from filter_config_envoy_ptr.
 * @return the result of the operation.
 */
envoy_dynamic_module_type_metrics_result
envoy_dynamic_module_callback_http_filter_config_define_counter(
    envoy_dynamic_module_type_http_filter_config_envoy_ptr filter_config_envoy_ptr,
    envoy_dynamic_module_type_buffer_module_ptr name, size_t name_length, size_t* counter_id_ptr);

/**
 * envoy_dynamic_module_callback_http_filter_config_define_counter_vec is called by the module
 * during initialization to create a template for generating Stats::Counters with the given name and
 * labels during the lifecycle of the module.
 *
 * @param filter_config_envoy_ptr is the pointer to the DynamicModuleHttpFilterConfig in which the
 * counter will be defined.
 * @param name is the name of the counter to be defined.
 * @param name_length is the length of the name.
 * @param label_names is the labels of the counter to be defined.
 * @param label_names_length is the length of the label_names.
 * @param counter_id_ptr where the opaque ID that represents a unique metric will be stored. This
 * can be passed to envoy_dynamic_module_callback_http_filter_increment_counter together with
 * filter_envoy_ptr created from filter_config_envoy_ptr.
 * @return the result of the operation.
 */
envoy_dynamic_module_type_metrics_result
envoy_dynamic_module_callback_http_filter_config_define_counter_vec(
    envoy_dynamic_module_type_http_filter_config_envoy_ptr filter_config_envoy_ptr,
    envoy_dynamic_module_type_buffer_module_ptr name, size_t name_length,
    envoy_dynamic_module_type_module_buffer* label_names, size_t label_names_length,
    size_t* counter_id_ptr);

/**
 * envoy_dynamic_module_callback_http_filter_increment_counter is called by the module to increment
 * a previously defined counter.
 *
 * @param filter_envoy_ptr is the pointer to the DynamicModuleHttpFilter object.
 * @param id is the ID of the counter previously defined using the config that created
 * filter_envoy_ptr
 * @param value is the value to increment the counter by.
 * @return the result of the operation.
 */
envoy_dynamic_module_type_metrics_result
envoy_dynamic_module_callback_http_filter_increment_counter(
    envoy_dynamic_module_type_http_filter_envoy_ptr filter_envoy_ptr, size_t id, uint64_t value);

/**
 * envoy_dynamic_module_callback_http_filter_increment_counter_vec is called by the module to
 * increment a previously defined counter vec.
 *
 * @param filter_envoy_ptr is the pointer to the DynamicModuleHttpFilter object.
 * @param id is the ID of the counter previously defined using the config that created
 * filter_envoy_ptr
 * @param label_values is the values of the labels to be incremented.
 * @param label_values_length is the length of the label_values.
 * @param value is the value to increment the counter by.
 * @return the result of the operation.
 */
envoy_dynamic_module_type_metrics_result
envoy_dynamic_module_callback_http_filter_increment_counter_vec(
    envoy_dynamic_module_type_http_filter_envoy_ptr filter_envoy_ptr, size_t id,
    envoy_dynamic_module_type_module_buffer* label_values, size_t label_values_length,
    uint64_t value);

/**
 * envoy_dynamic_module_callback_http_filter_config_define_gauge is called by the module during
 * initialization to create a new Stats::Gauge with the given name.
 *
 * @param filter_config_envoy_ptr is the pointer to the DynamicModuleHttpFilterConfig in which the
 * gauge will be defined.
 * @param name is the name of the gauge to be defined.
 * @param name_length is the length of the name.
 * @param gauge_id_ptr where the opaque ID that represents a unique metric will be stored. This can
 * be passed to envoy_dynamic_module_callback_http_filter_increment_gauge together with
 * filter_envoy_ptr created from filter_config_envoy_ptr.
 */
envoy_dynamic_module_type_metrics_result
envoy_dynamic_module_callback_http_filter_config_define_gauge(
    envoy_dynamic_module_type_http_filter_config_envoy_ptr filter_config_envoy_ptr,
    envoy_dynamic_module_type_buffer_module_ptr name, size_t name_length, size_t* gauge_id_ptr);

/**
 * envoy_dynamic_module_callback_http_filter_config_define_gauge_vec is called by the module during
 * initialization to create a template for generating Stats::Gauges with the given name and labels
 * during the lifecycle of the module.
 *
 * @param filter_config_envoy_ptr is the pointer to the DynamicModuleHttpFilterConfig in which the
 * gauge will be defined.
 * @param name is the name of the gauge to be defined.
 * @param name_length is the length of the name.
 * @param label_names is the labels of the gauge to be defined.
 * @param label_names_length is the length of the label_names.
 * @param gauge_id_ptr where the opaque ID that represents a unique metric will be stored. This can
 * be passed to envoy_dynamic_module_callback_http_filter_increment_gauge together with
 * filter_envoy_ptr created from filter_config_envoy_ptr.
 * @return the result of the operation.
 */
envoy_dynamic_module_type_metrics_result
envoy_dynamic_module_callback_http_filter_config_define_gauge_vec(
    envoy_dynamic_module_type_http_filter_config_envoy_ptr filter_config_envoy_ptr,
    envoy_dynamic_module_type_buffer_module_ptr name, size_t name_length,
    envoy_dynamic_module_type_module_buffer* label_names, size_t label_names_length,
    size_t* gauge_id_ptr);

/**
 * envoy_dynamic_module_callback_http_filter_increase_gauge is called by the module to increase the
 * value of a previously defined gauge.
 *
 * @param filter_envoy_ptr is the pointer to the DynamicModuleHttpFilter object.
 * @param id is the ID of the gauge previously defined using the config that created
 * filter_envoy_ptr
 * @param value is the value to increase the gauge by.
 * @return the result of the operation.
 */
envoy_dynamic_module_type_metrics_result envoy_dynamic_module_callback_http_filter_increase_gauge(
    envoy_dynamic_module_type_http_filter_envoy_ptr filter_envoy_ptr, size_t id, uint64_t value);

/**
 * envoy_dynamic_module_callback_http_filter_increase_gauge_vec is called by the module to increase
 * the value of a previously defined gauge vec.
 *
 * @param filter_envoy_ptr is the pointer to the DynamicModuleHttpFilter object.
 * @param id is the ID of the gauge previously defined using the config that created
 * filter_envoy_ptr
 * @param label_values is the values of the labels to be increased.
 * @param label_values_length is the length of the label_values.
 * @param value is the value to increase the gauge by.
 * @return the result of the operation.
 */
envoy_dynamic_module_type_metrics_result
envoy_dynamic_module_callback_http_filter_increase_gauge_vec(
    envoy_dynamic_module_type_http_filter_envoy_ptr filter_envoy_ptr, size_t id,
    envoy_dynamic_module_type_module_buffer* label_values, size_t label_values_length,
    uint64_t value);

/**
 * envoy_dynamic_module_callback_http_filter_decrease_gauge is called by the module to decrease the
 * value of a previously defined gauge.
 *
 * @param filter_envoy_ptr is the pointer to the DynamicModuleHttpFilter object.
 * @param id is the ID of the gauge previously defined using the config that created
 * filter_envoy_ptr
 * @param value is the value to decrease the gauge by.
 * @return the result of the operation.
 */
envoy_dynamic_module_type_metrics_result envoy_dynamic_module_callback_http_filter_decrease_gauge(
    envoy_dynamic_module_type_http_filter_envoy_ptr filter_envoy_ptr, size_t id, uint64_t value);

/**
 * envoy_dynamic_module_callback_http_filter_decrease_gauge_vec is called by the module to decrease
 * the value of a previously defined gauge vec.
 *
 * @param filter_envoy_ptr is the pointer to the DynamicModuleHttpFilter object.
 * @param id is the ID of the gauge previously defined using the config that created
 * filter_envoy_ptr
 * @param label_values is the values of the labels to be decreased.
 * @param label_values_length is the length of the label_values.
 * @param value is the value to decrease the gauge by.
 * @return the result of the operation.
 */
envoy_dynamic_module_type_metrics_result
envoy_dynamic_module_callback_http_filter_decrease_gauge_vec(
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
 * @param value is the value to set the gauge to.
 * @return the result of the operation.
 */
envoy_dynamic_module_type_metrics_result envoy_dynamic_module_callback_http_filter_set_gauge(
    envoy_dynamic_module_type_http_filter_envoy_ptr filter_envoy_ptr, size_t id, uint64_t value);

/**
 * envoy_dynamic_module_callback_http_filter_set_gauge_vec is called by the module to set the value
 * of a previously defined gauge vec.
 *
 * @param filter_envoy_ptr is the pointer to the DynamicModuleHttpFilter object.
 * @param id is the ID of the gauge previously defined using the config that created
 * filter_envoy_ptr
 * @param label_values is the values of the labels to be set.
 * @param label_values_length is the length of the label_values.
 * @param value is the value to set the gauge to.
 * @return the result of the operation.
 */
envoy_dynamic_module_type_metrics_result envoy_dynamic_module_callback_http_filter_set_gauge_vec(
    envoy_dynamic_module_type_http_filter_envoy_ptr filter_envoy_ptr, size_t id,
    envoy_dynamic_module_type_module_buffer* label_values, size_t label_values_length,
    uint64_t value);

/**
 * envoy_dynamic_module_callback_http_filter_config_define_histogram is called by the module during
 * initialization to create a new Stats::Histogram with the given name.
 *
 * @param filter_config_envoy_ptr is the pointer to the DynamicModuleHttpFilterConfig in which the
 * histogram will be defined.
 * @param name is the name of the histogram to be defined.
 * @param name_length is the length of the name.
 * @param histogram_id_ptr where the opaque ID that represents a unique metric will be stored. This
 * can be passed to envoy_dynamic_module_callback_http_filter_increment_gauge together with
 * filter_envoy_ptr created from filter_config_envoy_ptr.
 * @return the result of the operation.
 */
envoy_dynamic_module_type_metrics_result
envoy_dynamic_module_callback_http_filter_config_define_histogram(
    envoy_dynamic_module_type_http_filter_config_envoy_ptr filter_config_envoy_ptr,
    envoy_dynamic_module_type_buffer_module_ptr name, size_t name_length, size_t* histogram_id_ptr);

/**
 * envoy_dynamic_module_callback_http_filter_config_define_histogram is called by the module during
 * initialization to create a template for generating Stats::Histograms with the given name and
 * labels during the lifecycle of the module.
 *
 * @param filter_config_envoy_ptr is the pointer to the DynamicModuleHttpFilterConfig in which the
 * histogram will be defined.
 * @param name is the name of the histogram to be defined.
 * @param name_length is the length of the name.
 * @param label_names is the labels of the histogram to be defined.
 * @param label_names_length is the length of the label_names.
 * @param histogram_id_ptr where the opaque ID that represents a unique metric will be stored. This
 * can be passed to envoy_dynamic_module_callback_http_filter_increment_gauge together with
 * filter_envoy_ptr created from filter_config_envoy_ptr.
 * @return the result of the operation.
 */
envoy_dynamic_module_type_metrics_result
envoy_dynamic_module_callback_http_filter_config_define_histogram_vec(
    envoy_dynamic_module_type_http_filter_config_envoy_ptr filter_config_envoy_ptr,
    envoy_dynamic_module_type_buffer_module_ptr name, size_t name_length,
    envoy_dynamic_module_type_module_buffer* label_names, size_t label_names_length,
    size_t* histogram_id_ptr);

/**
 * envoy_dynamic_module_callback_http_filter_record_histogram_value is called by the module to
 * record a value in a previously defined histogram.
 *
 * @param histogram_envoy_ptr is a pointer to a histogram previously defined using
 * envoy_dynamic_module_callback_http_define_histogram.
 * @param id is the ID of the histogram previously defined using the config that created
 * filter_envoy_ptr
 * @param value is the value to record in the histogram.
 * @return the result of the operation.
 */
envoy_dynamic_module_type_metrics_result
envoy_dynamic_module_callback_http_filter_record_histogram_value(
    envoy_dynamic_module_type_http_filter_envoy_ptr filter_envoy_ptr, size_t id, uint64_t value);

/**
 * envoy_dynamic_module_callback_http_filter_record_histogram_value is called by the module to
 * record a value in a previously defined histogram vec.
 *
 * @param histogram_envoy_ptr is a pointer to a histogram previously defined using
 * envoy_dynamic_module_callback_http_define_histogram.
 * @param id is the ID of the histogram previously defined using the config that created
 * filter_envoy_ptr
 * @param label_values is the values of the labels to be recorded.
 * @param label_values_length is the length of the label_values.
 * @param value is the value to record in the histogram.
 * @return the result of the operation.
 */
envoy_dynamic_module_type_metrics_result
envoy_dynamic_module_callback_http_filter_record_histogram_value_vec(
    envoy_dynamic_module_type_http_filter_envoy_ptr filter_envoy_ptr, size_t id,
    envoy_dynamic_module_type_module_buffer* label_values, size_t label_values_length,
    uint64_t value);

// ---------------------- HTTP Header/Trailer callbacks ------------------------

/**
 * envoy_dynamic_module_callback_http_get_request_header is called by the module to get the
 * value of the request header with the given key. Since a header can have multiple values, the
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
 * @param key is the key of the request header.
 * @param key_length is the length of the key.
 * @param result_buffer_ptr is the pointer to the pointer variable where the pointer to the buffer
 * of the value will be stored. If the key does not exist or the index is out of range, this will be
 * set to nullptr.
 * @param result_buffer_length_ptr is the pointer to the variable where the length of the buffer
 * will be stored. If the key does not exist or the index is out of range, this will be set to 0.
 * @param index is the index of the header value in the list of values for the given key.
 * @param optional_size is the pointer to the variable where the number of values for the given key
 * will be stored. This parameter can be null if the module does not need this information.
 * @return true if the operation is successful, false otherwise.
 *
 * Note that a header value is not guaranteed to be a valid UTF-8 string. The module must be careful
 * when interpreting the value as a string in the language of the module.
 *
 * The buffer pointed by the pointer stored in result_buffer_ptr is owned by Envoy, and they are
 * guaranteed to be valid until the end of the current event hook unless the setter callback is
 * called.
 */
bool envoy_dynamic_module_callback_http_get_request_header(
    envoy_dynamic_module_type_http_filter_envoy_ptr filter_envoy_ptr,
    envoy_dynamic_module_type_buffer_module_ptr key, size_t key_length,
    envoy_dynamic_module_type_buffer_envoy_ptr* result_buffer_ptr, size_t* result_buffer_length_ptr,
    size_t index, size_t* optional_size);

/**
 * envoy_dynamic_module_callback_http_get_request_trailer is exactly the same as the
 * envoy_dynamic_module_callback_http_get_request_header, but for the request trailers.
 * See the comments on envoy_dynamic_module_http_get_request_header_value for more details.
 */
bool envoy_dynamic_module_callback_http_get_request_trailer(
    envoy_dynamic_module_type_http_filter_envoy_ptr filter_envoy_ptr,
    envoy_dynamic_module_type_buffer_module_ptr key, size_t key_length,
    envoy_dynamic_module_type_buffer_envoy_ptr* result_buffer_ptr, size_t* result_buffer_length_ptr,
    size_t index, size_t* optional_size);

/**
 * envoy_dynamic_module_callback_http_get_response_header is exactly the same as the
 * envoy_dynamic_module_callback_http_get_request_header, but for the response headers.
 * See the comments on envoy_dynamic_module_callback_http_get_request_header for more details.
 */
bool envoy_dynamic_module_callback_http_get_response_header(
    envoy_dynamic_module_type_http_filter_envoy_ptr filter_envoy_ptr,
    envoy_dynamic_module_type_buffer_module_ptr key, size_t key_length,
    envoy_dynamic_module_type_buffer_envoy_ptr* result_buffer_ptr, size_t* result_buffer_length_ptr,
    size_t index, size_t* optional_size);

/**
 * envoy_dynamic_module_callback_http_get_response_trailer is exactly the same as the
 * envoy_dynamic_module_callback_http_get_request_header, but for the response trailers.
 * See the comments on envoy_dynamic_module_callback_http_get_request_header for more details.
 */
bool envoy_dynamic_module_callback_http_get_response_trailer(
    envoy_dynamic_module_type_http_filter_envoy_ptr filter_envoy_ptr,
    envoy_dynamic_module_type_buffer_module_ptr key, size_t key_length,
    envoy_dynamic_module_type_buffer_envoy_ptr* result_buffer_ptr, size_t* result_buffer_length_ptr,
    size_t index, size_t* optional_size);

/**
 * envoy_dynamic_module_callback_http_get_request_headers_size is called by the module to get the
 * number of request headers. Combined with envoy_dynamic_module_callback_http_get_request_headers,
 * this can be used to iterate over all request headers.
 *
 * @param filter_envoy_ptr is the pointer to the DynamicModuleHttpFilter object of the
 * corresponding HTTP filter.
 * @param size is the pointer to the variable where the number of request headers will be stored.
 * @return true if the operation is successful, false otherwise.
 */
bool envoy_dynamic_module_callback_http_get_request_headers_size(
    envoy_dynamic_module_type_http_filter_envoy_ptr filter_envoy_ptr, size_t* size);

/**
 * envoy_dynamic_module_callback_http_get_request_trailers_size is exactly the same as the
 * envoy_dynamic_module_callback_http_get_request_headers_size, but for the request trailers.
 * See the comments on envoy_dynamic_module_callback_http_get_request_headers_size for more
 * details.
 */
bool envoy_dynamic_module_callback_http_get_request_trailers_size(
    envoy_dynamic_module_type_http_filter_envoy_ptr filter_envoy_ptr, size_t* size);

/**
 * envoy_dynamic_module_callback_http_get_response_headers_size is exactly the same as the
 * envoy_dynamic_module_callback_http_get_request_headers_size, but for the response headers.
 * See the comments on envoy_dynamic_module_callback_http_get_request_headers_size for more
 * details.
 */
bool envoy_dynamic_module_callback_http_get_response_headers_size(
    envoy_dynamic_module_type_http_filter_envoy_ptr filter_envoy_ptr, size_t* size);

/**
 * envoy_dynamic_module_callback_http_get_response_trailers_size is exactly the same as the
 * envoy_dynamic_module_callback_http_get_request_headers_size, but for the response trailers.
 * See the comments on envoy_dynamic_module_callback_http_get_request_headers_size for more
 * details.
 */
bool envoy_dynamic_module_callback_http_get_response_trailers_size(
    envoy_dynamic_module_type_http_filter_envoy_ptr filter_envoy_ptr, size_t* size);

/**
 * envoy_dynamic_module_callback_http_get_request_headers is called by the module to get all the
 * request headers. The headers are returned as an array of
 * envoy_dynamic_module_type_envoy_http_header.
 *
 * PRECONDITION: The module must ensure that the result_headers is valid and has enough length to
 * store all the headers. The module can use
 * envoy_dynamic_module_callback_http_get_request_headers_size to get the number of headers before
 * calling this function.
 *
 * @param filter_envoy_ptr is the pointer to the DynamicModuleHttpFilter object of the
 * corresponding HTTP filter.
 * @param result_headers is the pointer to the array of envoy_dynamic_module_type_envoy_http_header
 * where the headers will be stored. The lifetime of the buffer of key and value of each header is
 * guaranteed until the end of the current event hook unless the setter callback are called.
 * @return true if the operation is successful, false otherwise.
 */
bool envoy_dynamic_module_callback_http_get_request_headers(
    envoy_dynamic_module_type_http_filter_envoy_ptr filter_envoy_ptr,
    envoy_dynamic_module_type_envoy_http_header* result_headers);

/**
 * envoy_dynamic_module_callback_http_get_request_trailers is exactly the same as the
 * envoy_dynamic_module_callback_http_get_request_headers, but for the request trailers.
 * See the comments on envoy_dynamic_module_callback_http_get_request_headers for more details.
 */
bool envoy_dynamic_module_callback_http_get_request_trailers(
    envoy_dynamic_module_type_http_filter_envoy_ptr filter_envoy_ptr,
    envoy_dynamic_module_type_envoy_http_header* result_headers);

/**
 * envoy_dynamic_module_callback_http_get_response_headers is exactly the same as the
 * envoy_dynamic_module_callback_http_get_request_headers, but for the response headers.
 * See the comments on envoy_dynamic_module_callback_http_get_request_headers for more details.
 */
bool envoy_dynamic_module_callback_http_get_response_headers(
    envoy_dynamic_module_type_http_filter_envoy_ptr filter_envoy_ptr,
    envoy_dynamic_module_type_envoy_http_header* result_headers);

/**
 * envoy_dynamic_module_callback_http_get_response_trailers is exactly the same as the
 * envoy_dynamic_module_callback_http_get_request_headers, but for the response trailers.
 * See the comments on envoy_dynamic_module_callback_http_get_request_headers for more details.
 */
bool envoy_dynamic_module_callback_http_get_response_trailers(
    envoy_dynamic_module_type_http_filter_envoy_ptr filter_envoy_ptr,
    envoy_dynamic_module_type_envoy_http_header* result_headers);

/**
 * envoy_dynamic_module_callback_http_add_request_header is called by the module to add
 * the value of the request header with the given key. If the header does not exist, it will be
 * created. If the header already exists, all existing values will be removed and the new value will
 * be set. When the given value is null, the header will be removed if the key exists.
 *
 * @param filter_envoy_ptr is the pointer to the DynamicModuleHttpFilter object of the
 * corresponding HTTP filter.
 * @param key is the key of the header.
 * @param key_length is the length of the key.
 * @param value is the pointer to the buffer of the value. It can be null to remove the header.
 * @param value_length is the length of the value.
 * @return true if the operation is successful, false otherwise.
 *
 * Note that this only adds the header to the underlying Envoy object. Whether or not the header is
 * actually sent to the upstream depends on the phase of the execution and subsequent
 * filters. In other words, returning true from this function does not guarantee that the header
 * will be sent to the upstream.
 */
bool envoy_dynamic_module_callback_http_add_request_header(
    envoy_dynamic_module_type_http_filter_envoy_ptr filter_envoy_ptr,
    envoy_dynamic_module_type_buffer_module_ptr key, size_t key_length,
    envoy_dynamic_module_type_buffer_module_ptr value, size_t value_length);

/**
 * envoy_dynamic_module_callback_http_add_request_trailer is exactly the same as the
 * envoy_dynamic_module_callback_http_add_request_header, but for the request trailers.
 * See the comments on envoy_dynamic_module_callback_http_add_request_header for more details.
 */
bool envoy_dynamic_module_callback_http_add_request_trailer(
    envoy_dynamic_module_type_http_filter_envoy_ptr filter_envoy_ptr,
    envoy_dynamic_module_type_buffer_module_ptr key, size_t key_length,
    envoy_dynamic_module_type_buffer_module_ptr value, size_t value_length);

/**
 * envoy_dynamic_module_callback_http_add_response_header is exactly the same as the
 * envoy_dynamic_module_callback_http_add_request_header, but for the response headers.
 * See the comments on envoy_dynamic_module_callback_http_add_request_header for more details.
 */
bool envoy_dynamic_module_callback_http_add_response_header(
    envoy_dynamic_module_type_http_filter_envoy_ptr filter_envoy_ptr,
    envoy_dynamic_module_type_buffer_module_ptr key, size_t key_length,
    envoy_dynamic_module_type_buffer_module_ptr value, size_t value_length);

/**
 * envoy_dynamic_module_callback_http_add_response_trailer is exactly the same as the
 * envoy_dynamic_module_callback_http_add_request_header, but for the response trailers.
 * See the comments on envoy_dynamic_module_callback_http_add_request_header for more details.
 */
bool envoy_dynamic_module_callback_http_add_response_trailer(
    envoy_dynamic_module_type_http_filter_envoy_ptr filter_envoy_ptr,
    envoy_dynamic_module_type_buffer_module_ptr key, size_t key_length,
    envoy_dynamic_module_type_buffer_module_ptr value, size_t value_length);

/**
 * envoy_dynamic_module_callback_http_set_request_header is called by the module to set
 * the value of the request header with the given key. If the header does not exist, it will be
 * created. If the header already exists, all existing values will be removed and the new value will
 * be set. When the given value is null, the header will be removed if the key exists.
 *
 * @param filter_envoy_ptr is the pointer to the DynamicModuleHttpFilter object of the
 * corresponding HTTP filter.
 * @param key is the key of the header.
 * @param key_length is the length of the key.
 * @param value is the pointer to the buffer of the value. It can be null to remove the header.
 * @param value_length is the length of the value.
 * @return true if the operation is successful, false otherwise.
 *
 * Note that this only sets the header to the underlying Envoy object. Whether or not the header is
 * actually sent to the upstream depends on the phase of the execution and subsequent
 * filters. In other words, returning true from this function does not guarantee that the header
 * will be sent to the upstream.
 */
bool envoy_dynamic_module_callback_http_set_request_header(
    envoy_dynamic_module_type_http_filter_envoy_ptr filter_envoy_ptr,
    envoy_dynamic_module_type_buffer_module_ptr key, size_t key_length,
    envoy_dynamic_module_type_buffer_module_ptr value, size_t value_length);

/**
 * envoy_dynamic_module_callback_http_set_request_trailer is exactly the same as the
 * envoy_dynamic_module_callback_http_set_request_header, but for the request trailers.
 * See the comments on envoy_dynamic_module_callback_http_set_request_header for more details.
 */
bool envoy_dynamic_module_callback_http_set_request_trailer(
    envoy_dynamic_module_type_http_filter_envoy_ptr filter_envoy_ptr,
    envoy_dynamic_module_type_buffer_module_ptr key, size_t key_length,
    envoy_dynamic_module_type_buffer_module_ptr value, size_t value_length);

/**
 * envoy_dynamic_module_callback_http_set_response_header is exactly the same as the
 * envoy_dynamic_module_callback_http_set_request_header, but for the response headers.
 * See the comments on envoy_dynamic_module_callback_http_set_request_header for more details.
 */
bool envoy_dynamic_module_callback_http_set_response_header(
    envoy_dynamic_module_type_http_filter_envoy_ptr filter_envoy_ptr,
    envoy_dynamic_module_type_buffer_module_ptr key, size_t key_length,
    envoy_dynamic_module_type_buffer_module_ptr value, size_t value_length);

/**
 * envoy_dynamic_module_callback_http_set_response_trailer is exactly the same as the
 * envoy_dynamic_module_callback_http_set_request_header, but for the response trailers.
 * See the comments on envoy_dynamic_module_callback_http_set_request_header for more details.
 */
bool envoy_dynamic_module_callback_http_set_response_trailer(
    envoy_dynamic_module_type_http_filter_envoy_ptr filter_envoy_ptr,
    envoy_dynamic_module_type_buffer_module_ptr key, size_t key_length,
    envoy_dynamic_module_type_buffer_module_ptr value, size_t value_length);

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
 * @param body is the pointer to the buffer of the body of the response.
 * @param body_length is the length of the body.
 */
void envoy_dynamic_module_callback_http_send_response(
    envoy_dynamic_module_type_http_filter_envoy_ptr filter_envoy_ptr, uint32_t status_code,
    envoy_dynamic_module_type_module_http_header* headers_vector, size_t headers_vector_size,
    envoy_dynamic_module_type_buffer_module_ptr body, size_t body_length);

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
 * envoy_dynamic_module_callback_http_send_response is called by the module to send response
 * data to the downstream, optionally ending the stream.
 *
 * @param filter_envoy_ptr is the pointer to the DynamicModuleHttpFilter object of the
 * corresponding HTTP filter.
 * @param body is the pointer to the buffer of the body of the response.
 * @param body_length is the length of the body.
 * @param end_stream is a boolean indicating whether to end the stream.
 */
void envoy_dynamic_module_callback_http_send_response_data(
    envoy_dynamic_module_type_http_filter_envoy_ptr filter_envoy_ptr,
    envoy_dynamic_module_type_buffer_module_ptr data, size_t length, bool end_stream);

/**
 * envoy_dynamic_module_callback_http_send_response_headers is called by the module to send the
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
 * envoy_dynamic_module_callback_http_get_received_request_body_size is called by the module
 * to get the total bytes of buffers in the current request body.
 *
 * @param filter_envoy_ptr is the pointer to the DynamicModuleHttpFilter object of the
 * corresponding HTTP filter.
 * @param size is the pointer to the variable where the number of buffers will be stored.
 * @return true if the body is available, false otherwise.
 */
bool envoy_dynamic_module_callback_http_get_received_request_body_size(
    envoy_dynamic_module_type_http_filter_envoy_ptr filter_envoy_ptr, size_t* size);

/**
 * envoy_dynamic_module_callback_http_get_buffered_request_body_size is called by the module
 * to get the total bytes of buffers in the current request body.
 *
 * @param filter_envoy_ptr is the pointer to the DynamicModuleHttpFilter object of the
 * corresponding HTTP filter.
 * @param size is the pointer to the variable where the number of buffers will be stored.
 * @return true if the body is available, false otherwise.
 */
bool envoy_dynamic_module_callback_http_get_buffered_request_body_size(
    envoy_dynamic_module_type_http_filter_envoy_ptr filter_envoy_ptr, size_t* size);

/**
 * This is the same as envoy_dynamic_module_callback_http_get_received_request_body_size, but for
 * the current response body. See the comments on
 * envoy_dynamic_module_callback_http_get_received_request_body_size for more details.
 */
bool envoy_dynamic_module_callback_http_get_received_response_body_size(
    envoy_dynamic_module_type_http_filter_envoy_ptr filter_envoy_ptr, size_t* size);

/**
 * This is the same as envoy_dynamic_module_callback_http_get_buffered_request_body_size, but for
 * the buffered response body. See the comments on
 * envoy_dynamic_module_callback_http_get_buffered_request_body_size for more details.
 */
bool envoy_dynamic_module_callback_http_get_buffered_response_body_size(
    envoy_dynamic_module_type_http_filter_envoy_ptr filter_envoy_ptr, size_t* size);

/**
 * envoy_dynamic_module_callback_http_get_received_request_body_chunks is called by the module to
 * get the current request body as a vector of buffers. The body is returned as an array of
 * envoy_dynamic_module_type_envoy_buffer.
 *
 * PRECONDITION: The module must ensure that the result_buffer_vector is valid and has enough length
 * to store all the buffers. The module can use
 * envoy_dynamic_module_callback_http_get_received_request_body_chunks_size to get the number of
 * buffers before calling this function.
 *
 * @param filter_envoy_ptr is the pointer to the DynamicModuleHttpFilter object of the
 * corresponding HTTP filter.
 * @param result_buffer_vector is the pointer to the array of envoy_dynamic_module_type_envoy_buffer
 * where the buffers of the body will be stored. The lifetime of the buffer is guaranteed until the
 * end of the current event hook unless the setter callback is called.
 * @return true if the body is available, false otherwise.
 */
bool envoy_dynamic_module_callback_http_get_received_request_body_chunks(
    envoy_dynamic_module_type_http_filter_envoy_ptr filter_envoy_ptr,
    envoy_dynamic_module_type_envoy_buffer* result_buffer_vector);

/**
 * envoy_dynamic_module_callback_http_get_buffered_request_body_chunks is called by the module to
 * get the buffered request body as a vector of buffers. The body is returned as an array of
 * envoy_dynamic_module_type_envoy_buffer.
 *
 * PRECONDITION: The module must ensure that the result_buffer_vector is valid and has enough length
 * to store all the buffers. The module can use
 * envoy_dynamic_module_callback_http_get_buffered_request_body_chunks_size to get the number of
 * buffers before calling this function.
 *
 * @param filter_envoy_ptr is the pointer to the DynamicModuleHttpFilter object of the
 * corresponding HTTP filter.
 * @param result_buffer_vector is the pointer to the array of envoy_dynamic_module_type_envoy_buffer
 * where the buffers of the body will be stored. The lifetime of the buffer is guaranteed until the
 * end of the current event hook unless the setter callback is called.
 * @return true if the body is available, false otherwise.
 */
bool envoy_dynamic_module_callback_http_get_buffered_request_body_chunks(
    envoy_dynamic_module_type_http_filter_envoy_ptr filter_envoy_ptr,
    envoy_dynamic_module_type_envoy_buffer* result_buffer_vector);

/**
 * This is the same as envoy_dynamic_module_callback_http_get_received_request_body_chunks, but for
 * the current response body. See the comments on
 * envoy_dynamic_module_callback_http_get_received_request_body_chunks for more details.
 */
bool envoy_dynamic_module_callback_http_get_received_response_body_chunks(
    envoy_dynamic_module_type_http_filter_envoy_ptr filter_envoy_ptr,
    envoy_dynamic_module_type_envoy_buffer* result_buffer_vector);

/**
 * This is the same as envoy_dynamic_module_callback_http_get_buffered_request_body_chunks, but for
 * the buffered response body. See the comments on
 * envoy_dynamic_module_callback_http_get_buffered_request_body_chunks for more details.
 */
bool envoy_dynamic_module_callback_http_get_buffered_response_body_chunks(
    envoy_dynamic_module_type_http_filter_envoy_ptr filter_envoy_ptr,
    envoy_dynamic_module_type_envoy_buffer* result_buffer_vector);

/**
 * envoy_dynamic_module_callback_http_get_received_request_body_chunks_size is called by the module
 * to get the number of buffers in the current request body. Combined with
 * envoy_dynamic_module_callback_http_get_received_request_body_chunks, this can be used to iterate
 * over all buffers in the request body.
 *
 * @param filter_envoy_ptr is the pointer to the DynamicModuleHttpFilter object of the
 * corresponding HTTP filter.
 * @param size is the pointer to the variable where the number of buffers will be stored.
 * @return true if the body is available, false otherwise.
 */
bool envoy_dynamic_module_callback_http_get_received_request_body_chunks_size(
    envoy_dynamic_module_type_http_filter_envoy_ptr filter_envoy_ptr, size_t* size);

/**
 * envoy_dynamic_module_callback_http_get_buffered_request_body_chunks_size is called by the module
 * to get the number of buffers in the buffered request body. Combined with
 * envoy_dynamic_module_callback_http_get_buffered_request_body_chunks, this can be used to iterate
 * over all buffers in the request body.
 *
 * @param filter_envoy_ptr is the pointer to the DynamicModuleHttpFilter object of the
 * corresponding HTTP filter.
 * @param size is the pointer to the variable where the number of buffers will be stored.
 * @return true if the body is available, false otherwise.
 */
bool envoy_dynamic_module_callback_http_get_buffered_request_body_chunks_size(
    envoy_dynamic_module_type_http_filter_envoy_ptr filter_envoy_ptr, size_t* size);

/**
 * This is the same as envoy_dynamic_module_callback_http_get_received_request_body_chunks_size, but
 * for the current response body. See the comments on
 * envoy_dynamic_module_callback_http_get_received_request_body_chunks_size for more details.
 */
bool envoy_dynamic_module_callback_http_get_received_response_body_chunks_size(
    envoy_dynamic_module_type_http_filter_envoy_ptr filter_envoy_ptr, size_t* size);

/**
 * This is the same as envoy_dynamic_module_callback_http_get_buffered_request_body_chunks_size, but
 * for the buffered response body. See the comments on
 * envoy_dynamic_module_callback_http_get_buffered_request_body_chunks_size for more details.
 */
bool envoy_dynamic_module_callback_http_get_buffered_response_body_chunks_size(
    envoy_dynamic_module_type_http_filter_envoy_ptr filter_envoy_ptr, size_t* size);

/**
 * envoy_dynamic_module_callback_http_append_received_request_body is called by the module to append
 * the given data to the end of the current request body.
 *
 * @param filter_envoy_ptr is the pointer to the DynamicModuleHttpFilter object of the
 * corresponding HTTP filter.
 * @param data is the pointer to the buffer of the data to be appended.
 * @param length is the length of the data.
 * @return true if the body is available, false otherwise.
 */
bool envoy_dynamic_module_callback_http_append_received_request_body(
    envoy_dynamic_module_type_http_filter_envoy_ptr filter_envoy_ptr,
    envoy_dynamic_module_type_buffer_module_ptr data, size_t length);

/**
 * envoy_dynamic_module_callback_http_append_buffered_request_body is called by the module to append
 * the given data to the end of the buffered request body.
 *
 * @param filter_envoy_ptr is the pointer to the DynamicModuleHttpFilter object of the
 * corresponding HTTP filter.
 * @param data is the pointer to the buffer of the data to be appended.
 * @param length is the length of the data.
 * @return true if the body is available, false otherwise.
 */
bool envoy_dynamic_module_callback_http_append_buffered_request_body(
    envoy_dynamic_module_type_http_filter_envoy_ptr filter_envoy_ptr,
    envoy_dynamic_module_type_buffer_module_ptr data, size_t length);

/**
 * This is the same as envoy_dynamic_module_callback_http_append_received_request_body, but for the
 * current response body. See the comments on
 * envoy_dynamic_module_callback_http_append_received_request_body for more details.
 */
bool envoy_dynamic_module_callback_http_append_received_response_body(
    envoy_dynamic_module_type_http_filter_envoy_ptr filter_envoy_ptr,
    envoy_dynamic_module_type_buffer_module_ptr data, size_t length);

/**
 * This is the same as envoy_dynamic_module_callback_http_append_buffered_request_body, but for the
 * buffered response body. See the comments on
 * envoy_dynamic_module_callback_http_append_buffered_request_body for more details.
 */
bool envoy_dynamic_module_callback_http_append_buffered_response_body(
    envoy_dynamic_module_type_http_filter_envoy_ptr filter_envoy_ptr,
    envoy_dynamic_module_type_buffer_module_ptr data, size_t length);

/**
 * envoy_dynamic_module_callback_http_drain_received_request_body is called by the module to drain
 * the given number of bytes from the current request body. If the number of bytes to drain is
 * greater than the size of the body, the whole body will be drained.
 *
 * @param filter_envoy_ptr is the pointer to the DynamicModuleHttpFilter object of the
 * corresponding HTTP filter.
 * @param number_of_bytes is the number of bytes to drain.
 * @return true if the body is available, false otherwise.
 */
bool envoy_dynamic_module_callback_http_drain_received_request_body(
    envoy_dynamic_module_type_http_filter_envoy_ptr filter_envoy_ptr, size_t number_of_bytes);

/**
 * envoy_dynamic_module_callback_http_drain_buffered_request_body is called by the module to drain
 * the given number of bytes from the buffered request body. If the number of bytes to drain is
 * greater than the size of the body, the whole body will be drained.
 *
 * @param filter_envoy_ptr is the pointer to the DynamicModuleHttpFilter object of the
 * corresponding HTTP filter.
 * @param number_of_bytes is the number of bytes to drain.
 * @return true if the body is available, false otherwise.
 */
bool envoy_dynamic_module_callback_http_drain_buffered_request_body(
    envoy_dynamic_module_type_http_filter_envoy_ptr filter_envoy_ptr, size_t number_of_bytes);

/**
 * This is the same as envoy_dynamic_module_callback_http_drain_received_request_body, but for the
 * current response body. See the comments on
 * envoy_dynamic_module_callback_http_drain_received_request_body for more details.
 */
bool envoy_dynamic_module_callback_http_drain_received_response_body(
    envoy_dynamic_module_type_http_filter_envoy_ptr filter_envoy_ptr, size_t number_of_bytes);

/**
 * This is the same as envoy_dynamic_module_callback_http_drain_buffered_request_body, but for the
 * buffered response body. See the comments on
 * envoy_dynamic_module_callback_http_drain_buffered_request_body for more details.
 */
bool envoy_dynamic_module_callback_http_drain_buffered_response_body(
    envoy_dynamic_module_type_http_filter_envoy_ptr filter_envoy_ptr, size_t number_of_bytes);

// ---------------------------- Metadata Callbacks -----------------------------

/**
 * envoy_dynamic_module_callback_http_set_dynamic_metadata_number is called by the module to set
 * the number value of the dynamic metadata with the given namespace and key. If the metadata is not
 * accessible, this returns false. If the namespace does not exist, it will be created.
 *
 * @param filter_envoy_ptr is the pointer to the DynamicModuleHttpFilter object of the
 * corresponding HTTP filter.
 * @param namespace_ptr is the namespace of the dynamic metadata.
 * @param namespace_length is the length of the namespace.
 * @param key_ptr is the key of the dynamic metadata.
 * @param key_length is the length of the key.
 * @param value is the number value of the dynamic metadata to be set.
 * @return true if the operation is successful, false otherwise.
 */
bool envoy_dynamic_module_callback_http_set_dynamic_metadata_number(
    envoy_dynamic_module_type_http_filter_envoy_ptr filter_envoy_ptr,
    envoy_dynamic_module_type_buffer_module_ptr namespace_ptr, size_t namespace_length,
    envoy_dynamic_module_type_buffer_module_ptr key_ptr, size_t key_length, double value);

/**
 * envoy_dynamic_module_callback_http_get_dynamic_metadata_number is called by the module to get
 * the number value of the dynamic metadata with the given namespace and key. If the metadata is not
 * accessible, the namespace does not exist, the key does not exist or the value is not a number,
 * this returns false.
 *
 * @param filter_envoy_ptr is the pointer to the DynamicModuleHttpFilter object of the
 * corresponding HTTP filter.
 * @param namespace_ptr is the namespace of the dynamic metadata.
 * @param namespace_length is the length of the namespace.
 * @param key_ptr is the key of the dynamic metadata.
 * @param key_length is the length of the key.
 * @param result is the pointer to the variable where the number value of the dynamic metadata will
 * be stored.
 * @return true if the operation is successful, false otherwise.
 */
bool envoy_dynamic_module_callback_http_get_metadata_number(
    envoy_dynamic_module_type_http_filter_envoy_ptr filter_envoy_ptr,
    envoy_dynamic_module_type_metadata_source metadata_source,
    envoy_dynamic_module_type_buffer_module_ptr namespace_ptr, size_t namespace_length,
    envoy_dynamic_module_type_buffer_module_ptr key_ptr, size_t key_length, double* result);

/**
 * envoy_dynamic_module_callback_http_set_dynamic_metadata_string is called by the module to set
 * the string value of the dynamic metadata with the given namespace and key. If the metadata is not
 * accessible, this returns false. If the namespace does not exist, it will be created.
 *
 * @param filter_envoy_ptr is the pointer to the DynamicModuleHttpFilter object of the
 * corresponding HTTP filter.
 * @param namespace_ptr is the namespace of the dynamic metadata.
 * @param namespace_length is the length of the namespace.
 * @param key_ptr is the key of the dynamic metadata.
 * @param key_length is the length of the key.
 * @param value_ptr is the string value of the dynamic metadata to be set.
 * @param value_length is the length of the value.
 * @return true if the operation is successful, false otherwise.
 */
bool envoy_dynamic_module_callback_http_set_dynamic_metadata_string(
    envoy_dynamic_module_type_http_filter_envoy_ptr filter_envoy_ptr,
    envoy_dynamic_module_type_buffer_module_ptr namespace_ptr, size_t namespace_length,
    envoy_dynamic_module_type_buffer_module_ptr key_ptr, size_t key_length,
    envoy_dynamic_module_type_buffer_module_ptr value_ptr, size_t value_length);

/**
 * envoy_dynamic_module_callback_http_get_dynamic_metadata_string is called by the module to get
 * the string value of the dynamic metadata with the given namespace and key. If the metadata is not
 * accessible, the namespace does not exist, the key does not exist or the value is not a string,
 * this returns false.
 *
 * @param filter_envoy_ptr is the pointer to the DynamicModuleHttpFilter object of the
 * corresponding HTTP filter.
 * @param namespace_ptr is the namespace of the dynamic metadata.
 * @param namespace_length is the length of the namespace.
 * @param key_ptr is the key of the dynamic metadata.
 * @param key_length is the length of the key.
 * @param result_buffer_ptr is the pointer to the pointer variable where the pointer to the buffer
 * of the value will be stored.
 * @param result_buffer_length_ptr is the pointer to the variable where the length of the buffer
 * will be stored.
 * @return true if the operation is successful, false otherwise.
 *
 * Note that the buffer pointed by the pointer stored in result is owned by Envoy, and
 * they are guaranteed to be valid until the end of the current event hook unless the setter
 * callback is called.
 */
bool envoy_dynamic_module_callback_http_get_metadata_string(
    envoy_dynamic_module_type_http_filter_envoy_ptr filter_envoy_ptr,
    envoy_dynamic_module_type_metadata_source metadata_source,
    envoy_dynamic_module_type_buffer_module_ptr namespace_ptr, size_t namespace_length,
    envoy_dynamic_module_type_buffer_module_ptr key_ptr, size_t key_length,
    envoy_dynamic_module_type_buffer_envoy_ptr* result, size_t* result_length);

// -------------------------- Filter State Callbacks ---------------------------

/**
 * envoy_dynamic_module_callback_http_set_filter_state_bytes is called by the module to set the
 * bytes value of the filter state with the given key. If the filter state is not accessible, this
 * returns false. If the key does not exist, it will be created.
 *
 * @param filter_envoy_ptr is the pointer to the DynamicModuleHttpFilter object of the
 * corresponding HTTP filter.
 * @param key_ptr is the key of the filter state.
 * @param key_length is the length of the key.
 * @param value_ptr is the bytes value of the filter state to be set.
 * @param value_length is the length of the value.
 * @return true if the operation is successful, false otherwise.
 */
bool envoy_dynamic_module_callback_http_set_filter_state_bytes(
    envoy_dynamic_module_type_http_filter_envoy_ptr filter_envoy_ptr,
    envoy_dynamic_module_type_buffer_module_ptr key_ptr, size_t key_length,
    envoy_dynamic_module_type_buffer_module_ptr value_ptr, size_t value_length);

/**
 * envoy_dynamic_module_callback_http_get_filter_state_bytes is called by the module to get the
 * bytes value of the filter state with the given key. If the filter state is not accessible, the
 * key does not exist or the value is not bytes, this returns false.
 *
 * @param filter_envoy_ptr is the pointer to the DynamicModuleHttpFilter object of the
 * corresponding HTTP filter.
 * @param key_ptr is the key of the filter state.
 * @param key_length is the length of the key.
 * @param result_buffer_ptr is the pointer to the pointer variable where the pointer to the buffer
 * of the value will be stored.
 * @param result_buffer_length_ptr is the pointer to the variable where the length of the buffer
 * will be stored.
 * @return true if the operation is successful, false otherwise.
 *
 * Note that the buffer pointed by the pointer stored in result is owned by Envoy, and
 * they are guaranteed to be valid until the end of the current event hook unless the setter
 * callback is called.
 */
bool envoy_dynamic_module_callback_http_get_filter_state_bytes(
    envoy_dynamic_module_type_http_filter_envoy_ptr filter_envoy_ptr,
    envoy_dynamic_module_type_buffer_module_ptr key_ptr, size_t key_length,
    envoy_dynamic_module_type_buffer_envoy_ptr* result, size_t* result_length);

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
 * @param result_buffer_ptr is the pointer to the pointer variable where the pointer to the
 * buffer of the value will be stored.
 * @param result_length is the pointer to the variable where the length of the buffer will be
 * stored.
 * @return true if the operation is successful, false otherwise.
 *
 * Note: currently, not all attributes are implemented.
 */
bool envoy_dynamic_module_callback_http_filter_get_attribute_string(
    envoy_dynamic_module_type_http_filter_envoy_ptr filter_envoy_ptr,
    envoy_dynamic_module_type_attribute_id attribute_id,
    envoy_dynamic_module_type_buffer_envoy_ptr* result, size_t* result_length);

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
 * envoy_dynamic_module_callback_http_filter_http_callout is called by the module to initiate
 * an HTTP callout. The callout is initiated by the HTTP filter and the response is received in
 * envoy_dynamic_module_on_http_filter_http_callout_done.
 *
 * @param filter_envoy_ptr is the pointer to the DynamicModuleHttpFilter object of the
 * corresponding HTTP filter.
 * @param callout_id is the ID of the callout. This can be arbitrary and is used to
 * differentiate between multiple calls from the same filter.
 * @param cluster_name is the name of the cluster to which the callout is sent.
 * @param cluster_name_length is the length of the cluster name.
 * @param headers is the headers of the request. It must contain :method, :path and host headers.
 * @param headers_size is the size of the headers.
 * @param body is the pointer to the buffer of the body of the request.
 * @param body_size is the length of the body.
 * @param timeout_milliseconds is the timeout for the callout in milliseconds.
 * @return envoy_dynamic_module_type_http_callout_init_result is the result of the callout.
 */
envoy_dynamic_module_type_http_callout_init_result
envoy_dynamic_module_callback_http_filter_http_callout(
    envoy_dynamic_module_type_http_filter_envoy_ptr filter_envoy_ptr, uint32_t callout_id,
    envoy_dynamic_module_type_buffer_module_ptr cluster_name, size_t cluster_name_length,
    envoy_dynamic_module_type_module_http_header* headers, size_t headers_size,
    envoy_dynamic_module_type_buffer_module_ptr body, size_t body_size,
    uint64_t timeout_milliseconds);

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

#ifdef __cplusplus
}
#endif

// NOLINTEND
