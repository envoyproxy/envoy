// NOLINT(namespace-envoy)
#include "envoy/http/filter.h"

#include "library/common/extensions/filters/http/platform_bridge/c_types.h"

const envoy_filter_headers_status_t kEnvoyFilterHeadersStatusContinue =
    static_cast<envoy_filter_headers_status_t>(Envoy::Http::FilterHeadersStatus::Continue);
const envoy_filter_headers_status_t kEnvoyFilterHeadersStatusStopIteration =
    static_cast<envoy_filter_headers_status_t>(Envoy::Http::FilterHeadersStatus::StopIteration);
const envoy_filter_headers_status_t kEnvoyFilterHeadersStatusContinueAndEndStream =
    static_cast<envoy_filter_headers_status_t>(
        Envoy::Http::FilterHeadersStatus::ContinueAndEndStream);
const envoy_filter_headers_status_t kEnvoyFilterHeadersStatusStopAllIterationAndBuffer =
    static_cast<envoy_filter_headers_status_t>(
        Envoy::Http::FilterHeadersStatus::StopAllIterationAndBuffer);
// ResumeIteration is not a status supported by Envoy itself, and only has relevance in Envoy
// Mobile's implementation of platform filters.
//
// Regarding enum values, the C++11 standard (7.2/2) states:
//   If the first enumerator has no initializer, the value of the corresponding constant is zero.
//   An enumerator-definition without an initializer gives the enumerator the value obtained by
//   increasing the value of the previous enumerator by one.
//
// Creating a new return status like this is brittle, but at least somewhat more resilient to
// a new status being added in Envoy, since it won't overlap as long as the new status is added
// rather than prepended.
const envoy_filter_headers_status_t kEnvoyFilterHeadersStatusResumeIteration =
    kEnvoyFilterHeadersStatusContinue - 1;

const envoy_filter_data_status_t kEnvoyFilterDataStatusContinue =
    static_cast<envoy_filter_data_status_t>(Envoy::Http::FilterDataStatus::Continue);
const envoy_filter_data_status_t kEnvoyFilterDataStatusStopIterationAndBuffer =
    static_cast<envoy_filter_data_status_t>(Envoy::Http::FilterDataStatus::StopIterationAndBuffer);
const envoy_filter_data_status_t kEnvoyFilterDataStatusStopIterationNoBuffer =
    static_cast<envoy_filter_data_status_t>(Envoy::Http::FilterDataStatus::StopIterationNoBuffer);
// See comment above.
const envoy_filter_data_status_t kEnvoyFilterDataStatusResumeIteration =
    kEnvoyFilterDataStatusContinue - 1;

const envoy_filter_trailers_status_t kEnvoyFilterTrailersStatusContinue =
    static_cast<envoy_filter_trailers_status_t>(Envoy::Http::FilterTrailersStatus::Continue);
const envoy_filter_trailers_status_t kEnvoyFilterTrailersStatusStopIteration =
    static_cast<envoy_filter_trailers_status_t>(Envoy::Http::FilterTrailersStatus::StopIteration);
// See comment above.
const envoy_filter_trailers_status_t kEnvoyFilterTrailersStatusResumeIteration =
    kEnvoyFilterTrailersStatusContinue - 1;

// These values don't exist in Envoy and are essentially arbitrary.
const envoy_filter_resume_status_t kEnvoyFilterResumeStatusStopIteration = 1;
const envoy_filter_resume_status_t kEnvoyFilterResumeStatusResumeIteration = -1;
