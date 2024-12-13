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

#ifdef __cplusplus
#include <cstdbool>
#include <cstddef>

extern "C" {
#else

#include <stdbool.h>
#include <stddef.h>
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
 * envoy_dynamic_module_type_abi_version_envoy_ptr represents a null-terminated string that
 * contains the ABI version of the dynamic module. This is used to ensure that the dynamic module is
 * built against the compatible version of the ABI.
 *
 * OWNERSHIP: Envoy owns the pointer.
 */
typedef const char* envoy_dynamic_module_type_abi_version_envoy_ptr;

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
typedef const void* envoy_dynamic_module_type_http_filter_config_envoy_ptr;

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
 * envoy_dynamic_module_type_http_filter_envoy_ptr is a raw pointer to the DynamicModuleHttpFilter
 * class in Envoy. This is passed to the module when creating a new HTTP filter for each HTTP stream
 * and used to access the HTTP filter-scoped information such as headers, body, trailers, etc.
 *
 * This has 1:1 correspondence with envoy_dynamic_module_type_http_filter_module_ptr in the module.
 *
 * OWNERSHIP: Envoy owns the pointer, and can be accessed by the module until the filter is
 * destroyed, i.e. envoy_dynamic_module_on_http_filter_destroy is called.
 */
typedef const void* envoy_dynamic_module_type_http_filter_envoy_ptr;

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
 * envoy_dynamic_module_type_on_http_filter_request_headers_status represents the status of the
 * filter after processing the HTTP request headers. This corresponds to `FilterHeadersStatus` in
 * envoy/http/filter.h.
 */
typedef enum {
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
typedef enum {
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
typedef enum {
  envoy_dynamic_module_type_on_http_filter_request_trailers_status_Continue,
  envoy_dynamic_module_type_on_http_filter_request_trailers_status_StopIteration
} envoy_dynamic_module_type_on_http_filter_request_trailers_status;

/**
 * envoy_dynamic_module_type_on_http_filter_response_headers_status represents the status of the
 * filter after processing the HTTP response headers. This corresponds to `FilterHeadersStatus` in
 * envoy/http/filter.h.
 */
typedef enum {
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
typedef enum {
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
typedef enum {
  envoy_dynamic_module_type_on_http_filter_response_trailers_status_Continue,
  envoy_dynamic_module_type_on_http_filter_response_trailers_status_StopIteration
} envoy_dynamic_module_type_on_http_filter_response_trailers_status;

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
 * @return envoy_dynamic_module_type_abi_version_envoy_ptr is the ABI version of the dynamic
 * module. Null means the error and the module will be unloaded immediately.
 */
envoy_dynamic_module_type_abi_version_envoy_ptr envoy_dynamic_module_on_program_init();

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
    const char* name_ptr, size_t name_size, const char* config_ptr, size_t config_size);

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
 * envoy_dynamic_module_on_http_filter_destroy is called when the HTTP filter is destroyed for each
 * HTTP stream.
 *
 * @param filter_module_ptr is the pointer to the in-module HTTP filter.
 */
void envoy_dynamic_module_on_http_filter_destroy(
    envoy_dynamic_module_type_http_filter_module_ptr filter_module_ptr);

#ifdef __cplusplus
}
#endif

// NOLINTEND
