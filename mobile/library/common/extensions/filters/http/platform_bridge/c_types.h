#pragma once

#include "library/common/types/c_types.h"

// NOLINT(namespace-envoy)

/**
 * Return codes for on-headers filter invocations. @see envoy/http/filter.h
 */
typedef enum {
  ENVOY_FILTER_HEADERS_STATUS_CONTINUE = 0,
  ENVOY_FILTER_HEADERS_STATUS_STOP_ITERATION,
  ENVOY_FILTER_HEADERS_STATUS_CONTINUE_AND_END_STREAM,
  ENVOY_FILTER_HEADERS_STATUS_STOP_ALL_ITERATION_AND_BUFFER,
} envoy_filter_headers_status_t;

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
typedef enum {
  ENVOY_FILTER_DATA_STATUS_CONTINUE = 0,
  ENVOY_FILTER_DATA_STATUS_STOP_ITERATION_AND_BUFFER,
  ENVOY_FILTER_DATA_STATUS_STOP_ITERATION_NO_BUFFER,
} envoy_filter_data_status_t;

/**
 * Compound return type for on-data filter invocations.
 */
typedef struct {
  envoy_filter_data_status_t status;
  envoy_data data;
} envoy_filter_data_status;

/**
 * Return codes for on-trailers filter invocations. @see envoy/http/filter.h
 */
typedef enum {
  ENVOY_FILTER_TRAILERS_STATUS_CONTINUE = 0,
  ENVOY_FILTER_TRAILERS_STATUS_STOP_ITERATION,
} envoy_filter_trailers_status_t;

/**
 * Compound return type for on-trailers filter invocations.
 */
typedef struct {
  envoy_filter_trailers_status_t status;
  envoy_headers trailers;
} envoy_filter_trailers_status;

#ifdef __cplusplus
extern "C" { // function pointers
#endif

/**
 * Function signature for on-headers filter invocations.
 */
typedef envoy_filter_headers_status (*envoy_filter_on_headers_f)(envoy_headers headers,
                                                                 bool end_stream, void* context);

/**
 * Function signature for on-data filter invocations.
 */
typedef envoy_filter_data_status (*envoy_filter_on_data_f)(envoy_data data, bool end_stream,
                                                           void* context);

/**
 * Function signature for on-trailers filter invocations.
 */
typedef envoy_filter_trailers_status (*envoy_filter_on_trailers_f)(envoy_headers trailers,
                                                                   void* context);

#ifdef __cplusplus
} // function pointers
#endif

/**
 * Raw datatype containing dispatch functions for a platform-native HTTP filter. Leveraged by the
 * PlatformBridgeFilter.
 */
typedef struct {
  envoy_filter_on_headers_f on_request_headers;
  envoy_filter_on_data_f on_request_data;
  envoy_filter_on_trailers_f on_request_trailers;
  envoy_filter_on_headers_f on_response_headers;
  envoy_filter_on_data_f on_response_data;
  envoy_filter_on_trailers_f on_response_trailers;
  void* context;
} envoy_http_filter;
