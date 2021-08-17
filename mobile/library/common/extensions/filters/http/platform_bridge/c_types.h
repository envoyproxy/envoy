#pragma once

#include "library/common/types/c_types.h"

// NOLINT(namespace-envoy)

/**
 * Convenience constant indicating no changes to data.
 */
extern const envoy_data envoy_unaltered_data;

/**
 * Convenience constant indicating no changes to headers.
 */
extern const envoy_headers envoy_unaltered_headers;

/**
 * Return codes for on-headers filter invocations. @see envoy/http/filter.h
 */
typedef int envoy_filter_headers_status_t;
extern const envoy_filter_headers_status_t kEnvoyFilterHeadersStatusContinue;
extern const envoy_filter_headers_status_t kEnvoyFilterHeadersStatusStopIteration;
extern const envoy_filter_headers_status_t kEnvoyFilterHeadersStatusStopAllIterationAndBuffer;
// Note this return status is unique to platform filters and used only to resume iteration after
// it has been previously stopped.
extern const envoy_filter_headers_status_t kEnvoyFilterHeadersStatusResumeIteration;

/**
 * Compound return type for on-headers filter invocations.
 */
typedef struct {
  envoy_filter_headers_status_t status;
  envoy_headers headers;
} envoy_filter_headers_status;

/**
 * Return codes for on-data filter invocations. @see envoy/http/filter.h
 */
typedef int envoy_filter_data_status_t;
extern const envoy_filter_data_status_t kEnvoyFilterDataStatusContinue;
extern const envoy_filter_data_status_t kEnvoyFilterDataStatusStopIterationAndBuffer;
extern const envoy_filter_data_status_t kEnvoyFilterDataStatusStopIterationNoBuffer;
// Note this return status is unique to platform filters and used only to resume iteration after
// it has been previously stopped.
extern const envoy_filter_data_status_t kEnvoyFilterDataStatusResumeIteration;

/**
 * Compound return type for on-data filter invocations.
 */
typedef struct {
  envoy_filter_data_status_t status;
  envoy_data data;
  envoy_headers* pending_headers;
} envoy_filter_data_status;

/**
 * Return codes for on-trailers filter invocations. @see envoy/http/filter.h
 */
typedef int envoy_filter_trailers_status_t;
extern const envoy_filter_trailers_status_t kEnvoyFilterTrailersStatusContinue;
extern const envoy_filter_trailers_status_t kEnvoyFilterTrailersStatusStopIteration;
// Note this return status is unique to platform filters and used only to resume iteration after
// it has been previously stopped.
extern const envoy_filter_trailers_status_t kEnvoyFilterTrailersStatusResumeIteration;

/**
 * Compound return type for on-trailers filter invocations.
 */
typedef struct {
  envoy_filter_trailers_status_t status;
  envoy_headers trailers;
  envoy_headers* pending_headers;
  envoy_data* pending_data;
} envoy_filter_trailers_status;

/**
 * Return code for on-resume filter invocations. This is an invocation unique to platform
 * filters that provides an in-thread opportunity to read and modify pending stream state
 * upon asynchronous resumption of filter iteration.
 */
typedef int envoy_filter_resume_status_t;
extern const envoy_filter_resume_status_t kEnvoyFilterResumeStatusStopIteration;
extern const envoy_filter_resume_status_t kEnvoyFilterResumeStatusResumeIteration;

/**
 * Compound return type for on-resume filter invocations. It is a filter state
 * violation for the entities in the return status to be a different set from those passed as
 * parameters to the filter invocation.
 */
typedef struct {
  envoy_filter_resume_status_t status;
  envoy_headers* pending_headers;
  envoy_data* pending_data;
  envoy_headers* pending_trailers;
} envoy_filter_resume_status;

#ifdef __cplusplus
extern "C" { // function pointers
#endif

/**
 * Function signature for filter factory. Implementations must return a instance_context
 * capable of dispatching envoy_http_filter calls (below) to a platform filter instance.
 */
typedef const void* (*envoy_filter_init_f)(const void* context);

/**
 * Function signature for on-headers filter invocations.
 */
typedef envoy_filter_headers_status (*envoy_filter_on_headers_f)(envoy_headers headers,
                                                                 bool end_stream,
                                                                 envoy_stream_intel stream_intel,
                                                                 const void* context);

/**
 * Function signature for on-data filter invocations.
 */
typedef envoy_filter_data_status (*envoy_filter_on_data_f)(envoy_data data, bool end_stream,
                                                           envoy_stream_intel stream_intel,
                                                           const void* context);

/**
 * Function signature for on-trailers filter invocations.
 */
typedef envoy_filter_trailers_status (*envoy_filter_on_trailers_f)(envoy_headers trailers,
                                                                   envoy_stream_intel stream_intel,
                                                                   const void* context);

/**
 * Function signature for filter invocation after asynchronous resumption. Passes a
 * snapshot of all HTTP state that has not yet been forwarded along the filter chain.
 */
typedef envoy_filter_resume_status (*envoy_filter_on_resume_f)(
    envoy_headers* headers, envoy_data* data, envoy_headers* trailers, bool end_stream,
    envoy_stream_intel stream_intel, const void* context);

/**
 * Function signature for on-cancellation filter invocations.
 */
typedef void (*envoy_filter_on_cancel_f)(envoy_stream_intel stream_intel, const void* context);

/**
 * Function signature for on-error filter invocations.
 */
typedef void (*envoy_filter_on_error_f)(envoy_error error, envoy_stream_intel stream_intel,
                                        const void* context);

/**
 * Function signature to release a filter instance once the filter chain is finished with it.
 */
typedef void (*envoy_filter_release_f)(const void* context);

/**
 * Function signature for asynchronous filter callback to resume filter iteration.
 */
typedef void (*envoy_filter_resume_f)(const void* context);

/**
 * Function signature for async filter callback to reset stream idle timeout.
 */
typedef void (*envoy_filter_reset_idle_f)(const void* context);

/**
 * Raw datatype containing asynchronous callbacks for platform HTTP filters.
 */
typedef struct {
  envoy_filter_resume_f resume_iteration;
  envoy_filter_reset_idle_f reset_idle;
  envoy_filter_release_f release_callbacks;
  const void* callback_context;
} envoy_http_filter_callbacks;

typedef void (*envoy_filter_set_callbacks_f)(envoy_http_filter_callbacks callbacks,
                                             const void* context);

#ifdef __cplusplus
} // function pointers
#endif

/**
 * Raw datatype containing dispatch functions for a platform HTTP filter. Leveraged by the
 * PlatformBridgeFilter.
 */
typedef struct {
  envoy_filter_init_f init_filter;
  envoy_filter_on_headers_f on_request_headers;
  envoy_filter_on_data_f on_request_data;
  envoy_filter_on_trailers_f on_request_trailers;
  envoy_filter_on_headers_f on_response_headers;
  envoy_filter_on_data_f on_response_data;
  envoy_filter_on_trailers_f on_response_trailers;
  envoy_filter_set_callbacks_f set_request_callbacks;
  envoy_filter_on_resume_f on_resume_request;
  envoy_filter_set_callbacks_f set_response_callbacks;
  envoy_filter_on_resume_f on_resume_response;
  envoy_filter_on_cancel_f on_cancel;
  envoy_filter_on_error_f on_error;
  envoy_filter_release_f release_filter;
  const void* static_context;
  const void* instance_context;
} envoy_http_filter;
