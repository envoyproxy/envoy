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
 * envoy_dynamic_module_type_buffer_module_ptr is a pointer to a buffer in the module. A buffer
 * represents a contiguous block of memory in bytes.
 *
 * OWNERSHIP: The module is responsible for managing the lifetime of the pointer. It depends on the
 * context where the buffer is used. See for the specific event hook or callback for more details.
 */
typedef char* envoy_dynamic_module_type_buffer_module_ptr;

/**
 * envoy_dynamic_module_type_buffer_envoy_ptr is a pointer to a buffer in Envoy. A buffer represents
 * a contiguous block of memory in bytes.
 *
 * OWNERSHIP: Envoy owns the pointer. The lifetime depends on the context where the buffer is used.
 * See for the specific event hook or callback for more details.
 */
typedef char* envoy_dynamic_module_type_buffer_envoy_ptr;

/**
 * envoy_dynamic_module_type_envoy_buffer represents a buffer owned by Envoy.
 * This is to give the direct access to the buffer in Envoy.
 */
typedef struct {
  envoy_dynamic_module_type_buffer_envoy_ptr ptr;
  size_t length;
} envoy_dynamic_module_type_envoy_buffer;

/**
 * envoy_dynamic_module_type_module_http_header represents a key-value pair of an HTTP header owned
 * by the module.
 */
typedef struct {
  envoy_dynamic_module_type_buffer_module_ptr key_ptr;
  size_t key_length;
  envoy_dynamic_module_type_buffer_module_ptr value_ptr;
  size_t value_length;
} envoy_dynamic_module_type_module_http_header;

/**
 * envoy_dynamic_module_type_http_header represents a key-value pair of an HTTP header owned by
 * Envoy's HeaderMap.
 */
typedef struct {
  envoy_dynamic_module_type_buffer_envoy_ptr key_ptr;
  size_t key_length;
  envoy_dynamic_module_type_buffer_envoy_ptr value_ptr;
  size_t value_length;
} envoy_dynamic_module_type_http_header;

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
envoy_dynamic_module_type_abi_version_envoy_ptr envoy_dynamic_module_on_program_init(void);

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

// -----------------------------------------------------------------------------
// -------------------------------- Callbacks ----------------------------------
// -----------------------------------------------------------------------------
//
// Callbacks are functions implemented by Envoy that can be called by the module to interact with
// Envoy. The name of a callback must be prefixed with "envoy_dynamic_module_callback_".

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
 * @return the number of values for the given key, regardless of whether the value is found or not.
 *
 * Note that a header value is not guaranteed to be a valid UTF-8 string. The module must be careful
 * when interpreting the value as a string in the language of the module.
 *
 * The buffer pointed by the pointer stored in result_buffer_ptr is owned by Envoy, and they are
 * guaranteed to be valid until the end of the current event hook unless the setter callback is
 * called.
 */
size_t envoy_dynamic_module_callback_http_get_request_header(
    envoy_dynamic_module_type_http_filter_envoy_ptr filter_envoy_ptr,
    envoy_dynamic_module_type_buffer_module_ptr key, size_t key_length,
    envoy_dynamic_module_type_buffer_envoy_ptr* result_buffer_ptr, size_t* result_buffer_length_ptr,
    size_t index);

/**
 * envoy_dynamic_module_callback_http_get_request_trailer is exactly the same as the
 * envoy_dynamic_module_callback_http_get_request_header, but for the request trailers.
 * See the comments on envoy_dynamic_module_http_get_request_header_value for more details.
 */
size_t envoy_dynamic_module_callback_http_get_request_trailer(
    envoy_dynamic_module_type_http_filter_envoy_ptr filter_envoy_ptr,
    envoy_dynamic_module_type_buffer_module_ptr key, size_t key_length,
    envoy_dynamic_module_type_buffer_envoy_ptr* result_buffer_ptr, size_t* result_buffer_length_ptr,
    size_t index);

/**
 * envoy_dynamic_module_callback_http_get_response_header is exactly the same as the
 * envoy_dynamic_module_callback_http_get_request_header, but for the response headers.
 * See the comments on envoy_dynamic_module_callback_http_get_request_header for more details.
 */
size_t envoy_dynamic_module_callback_http_get_response_header(
    envoy_dynamic_module_type_http_filter_envoy_ptr filter_envoy_ptr,
    envoy_dynamic_module_type_buffer_module_ptr key, size_t key_length,
    envoy_dynamic_module_type_buffer_envoy_ptr* result_buffer_ptr, size_t* result_buffer_length_ptr,
    size_t index);

/**
 * envoy_dynamic_module_callback_http_get_response_trailer is exactly the same as the
 * envoy_dynamic_module_callback_http_get_request_header, but for the response trailers.
 * See the comments on envoy_dynamic_module_callback_http_get_request_header for more details.
 */
size_t envoy_dynamic_module_callback_http_get_response_trailer(
    envoy_dynamic_module_type_http_filter_envoy_ptr filter_envoy_ptr,
    envoy_dynamic_module_type_buffer_module_ptr key, size_t key_length,
    envoy_dynamic_module_type_buffer_envoy_ptr* result_buffer_ptr, size_t* result_buffer_length_ptr,
    size_t index);

/**
 * envoy_dynamic_module_callback_http_get_request_headers_count is called by the module to get the
 * number of request headers. Combined with envoy_dynamic_module_callback_http_get_request_headers,
 * this can be used to iterate over all request headers.
 *
 * @param filter_envoy_ptr is the pointer to the DynamicModuleHttpFilter object of the
 * corresponding HTTP filter.
 * @return the number of request headers. Returns zero if the headers are not available.
 */
size_t envoy_dynamic_module_callback_http_get_request_headers_count(
    envoy_dynamic_module_type_http_filter_envoy_ptr filter_envoy_ptr);

/**
 * envoy_dynamic_module_callback_http_get_request_trailers_count is exactly the same as the
 * envoy_dynamic_module_callback_http_get_request_headers_count, but for the request trailers.
 * See the comments on envoy_dynamic_module_callback_http_get_request_headers_count for more
 * details.
 */
size_t envoy_dynamic_module_callback_http_get_request_trailers_count(
    envoy_dynamic_module_type_http_filter_envoy_ptr filter_envoy_ptr);

/**
 * envoy_dynamic_module_callback_http_get_response_headers_count is exactly the same as the
 * envoy_dynamic_module_callback_http_get_request_headers_count, but for the response headers.
 * See the comments on envoy_dynamic_module_callback_http_get_request_headers_count for more
 * details.
 */
size_t envoy_dynamic_module_callback_http_get_response_headers_count(
    envoy_dynamic_module_type_http_filter_envoy_ptr filter_envoy_ptr);

/**
 * envoy_dynamic_module_callback_http_get_response_trailers_count is exactly the same as the
 * envoy_dynamic_module_callback_http_get_request_headers_count, but for the response trailers.
 * See the comments on envoy_dynamic_module_callback_http_get_request_headers_count for more
 * details.
 */
size_t envoy_dynamic_module_callback_http_get_response_trailers_count(
    envoy_dynamic_module_type_http_filter_envoy_ptr filter_envoy_ptr);

/**
 * envoy_dynamic_module_callback_http_get_request_headers is called by the module to get all the
 * request headers. The headers are returned as an array of envoy_dynamic_module_type_http_header.
 *
 * PRECONDITION: The module must ensure that the result_headers is valid and has enough length to
 * store all the headers. The module can use
 * envoy_dynamic_module_callback_http_get_request_headers_count to get the number of headers before
 * calling this function.
 *
 * @param filter_envoy_ptr is the pointer to the DynamicModuleHttpFilter object of the
 * corresponding HTTP filter.
 * @param result_headers is the pointer to the array of envoy_dynamic_module_type_http_header where
 * the headers will be stored. The lifetime of the buffer of key and value of each header is
 * guaranteed until the end of the current event hook unless the setter callback are called.
 * @return true if the operation is successful, false otherwise.
 */
bool envoy_dynamic_module_callback_http_get_request_headers(
    envoy_dynamic_module_type_http_filter_envoy_ptr filter_envoy_ptr,
    envoy_dynamic_module_type_http_header* result_headers);

/**
 * envoy_dynamic_module_callback_http_get_request_trailers is exactly the same as the
 * envoy_dynamic_module_callback_http_get_request_headers, but for the request trailers.
 * See the comments on envoy_dynamic_module_callback_http_get_request_headers for more details.
 */
bool envoy_dynamic_module_callback_http_get_request_trailers(
    envoy_dynamic_module_type_http_filter_envoy_ptr filter_envoy_ptr,
    envoy_dynamic_module_type_http_header* result_headers);

/**
 * envoy_dynamic_module_callback_http_get_response_headers is exactly the same as the
 * envoy_dynamic_module_callback_http_get_request_headers, but for the response headers.
 * See the comments on envoy_dynamic_module_callback_http_get_request_headers for more details.
 */
bool envoy_dynamic_module_callback_http_get_response_headers(
    envoy_dynamic_module_type_http_filter_envoy_ptr filter_envoy_ptr,
    envoy_dynamic_module_type_http_header* result_headers);

/**
 * envoy_dynamic_module_callback_http_get_response_trailers is exactly the same as the
 * envoy_dynamic_module_callback_http_get_request_headers, but for the response trailers.
 * See the comments on envoy_dynamic_module_callback_http_get_request_headers for more details.
 */
bool envoy_dynamic_module_callback_http_get_response_trailers(
    envoy_dynamic_module_type_http_filter_envoy_ptr filter_envoy_ptr,
    envoy_dynamic_module_type_http_header* result_headers);

/**
 * envoy_dynamic_module_callback_http_set_request_header is called by the module to set
 * the value of the request header with the given key. If the header does not exist, it will be
 * created. If the header already exists, all existing values will be removed and the new value will
 * be set.
 *
 * @param filter_envoy_ptr is the pointer to the DynamicModuleHttpFilter object of the
 * corresponding HTTP filter.
 * @param key is the key of the header.
 * @param key_length is the length of the key.
 * @param value is the value of the header.
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

// ------------------- HTTP Request/Response body callbacks --------------------

/**
 * envoy_dynamic_module_callback_http_get_request_body_vector is called by the module to get the
 * request body as a vector of buffers. The body is returned as an array of
 * envoy_dynamic_module_type_envoy_buffer.
 *
 * PRECONDITION: The module must ensure that the result_buffer_vector is valid and has enough length
 * to store all the buffers. The module can use
 * envoy_dynamic_module_callback_http_get_request_body_vector_size to get the number of buffers
 * before calling this function.
 *
 * @param filter_envoy_ptr is the pointer to the DynamicModuleHttpFilter object of the
 * corresponding HTTP filter.
 * @param result_buffer_vector is the pointer to the array of envoy_dynamic_module_type_envoy_buffer
 * where the buffers of the body will be stored. The lifetime of the buffer is guaranteed until the
 * end of the current event hook unless the setter callback is called.
 * @return true if the body is available, false otherwise.
 */
bool envoy_dynamic_module_callback_http_get_request_body_vector(
    envoy_dynamic_module_type_http_filter_envoy_ptr filter_envoy_ptr,
    envoy_dynamic_module_type_envoy_buffer* result_buffer_vector);

/**
 * envoy_dynamic_module_callback_http_get_request_body_vector_size is called by the module to get
 * the number of buffers in the request body. Combined with
 * envoy_dynamic_module_callback_http_get_request_body_vector, this can be used to iterate over all
 * buffers in the request body.
 *
 * @param filter_envoy_ptr is the pointer to the DynamicModuleHttpFilter object of the
 * corresponding HTTP filter.
 * @param size is the pointer to the variable where the number of buffers will be stored.
 * @return true if the body is available, false otherwise.
 */
bool envoy_dynamic_module_callback_http_get_request_body_vector_size(
    envoy_dynamic_module_type_http_filter_envoy_ptr filter_envoy_ptr, size_t* size);

/**
 * envoy_dynamic_module_callback_http_append_request_body is called by the module to append the
 * given data to the end of the request body.
 *
 * @param filter_envoy_ptr is the pointer to the DynamicModuleHttpFilter object of the
 * corresponding HTTP filter.
 * @param data is the pointer to the buffer of the data to be appended.
 * @param length is the length of the data.
 * @return true if the body is available, false otherwise.
 */
bool envoy_dynamic_module_callback_http_append_request_body(
    envoy_dynamic_module_type_http_filter_envoy_ptr filter_envoy_ptr,
    envoy_dynamic_module_type_buffer_module_ptr data, size_t length);

/**
 * envoy_dynamic_module_callback_http_drain_request_body is called by the module to drain the given
 * number of bytes from the request body. If the number of bytes to drain is greater than
 * the size of the body, the whole body will be drained.
 *
 * @param filter_envoy_ptr is the pointer to the DynamicModuleHttpFilter object of the
 * corresponding HTTP filter.
 * @param number_of_bytes is the number of bytes to drain.
 * @return true if the body is available, false otherwise.
 */
bool envoy_dynamic_module_callback_http_drain_request_body(
    envoy_dynamic_module_type_http_filter_envoy_ptr filter_envoy_ptr, size_t number_of_bytes);

/**
 * This is the same as envoy_dynamic_module_callback_http_get_request_body_vector, but for the
 * response body. See the comments on envoy_dynamic_module_callback_http_get_request_body_vector
 * for more details.
 */
bool envoy_dynamic_module_callback_http_get_response_body_vector(
    envoy_dynamic_module_type_http_filter_envoy_ptr filter_envoy_ptr,
    envoy_dynamic_module_type_envoy_buffer* result_buffer_vector);

/**
 * This is the same as envoy_dynamic_module_callback_http_get_request_body_vector_size, but for the
 * response body. See the comments on
 * envoy_dynamic_module_callback_http_get_request_body_vector_size for more details.
 */
bool envoy_dynamic_module_callback_http_get_response_body_vector_size(
    envoy_dynamic_module_type_http_filter_envoy_ptr filter_envoy_ptr, size_t* size);

/**
 * This is the same as envoy_dynamic_module_callback_http_append_request_body, but for the response
 * body. See the comments on envoy_dynamic_module_callback_http_append_request_body for more
 * details.
 */
bool envoy_dynamic_module_callback_http_append_response_body(
    envoy_dynamic_module_type_http_filter_envoy_ptr filter_envoy_ptr,
    envoy_dynamic_module_type_buffer_module_ptr data, size_t length);

/**
 * This is the same as envoy_dynamic_module_callback_http_drain_request_body, but for the response
 * body. See the comments on envoy_dynamic_module_callback_http_drain_request_body for more details.
 */
bool envoy_dynamic_module_callback_http_drain_response_body(
    envoy_dynamic_module_type_http_filter_envoy_ptr filter_envoy_ptr, size_t number_of_bytes);

// ------------------------ Dynamic Metadata Callbacks -------------------------

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
bool envoy_dynamic_module_callback_http_get_dynamic_metadata_number(
    envoy_dynamic_module_type_http_filter_envoy_ptr filter_envoy_ptr,
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
bool envoy_dynamic_module_callback_http_get_dynamic_metadata_string(
    envoy_dynamic_module_type_http_filter_envoy_ptr filter_envoy_ptr,
    envoy_dynamic_module_type_buffer_module_ptr namespace_ptr, size_t namespace_length,
    envoy_dynamic_module_type_buffer_module_ptr key_ptr, size_t key_length,
    envoy_dynamic_module_type_buffer_envoy_ptr* result, size_t* result_length);

#ifdef __cplusplus
}
#endif

// NOLINTEND
