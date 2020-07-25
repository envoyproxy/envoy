// NOLINT(namespace-envoy)
#include "library/common/extensions/filters/http/platform_bridge/c_types.h"

#include "envoy/http/filter.h"

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

const envoy_filter_data_status_t kEnvoyFilterDataStatusContinue =
    static_cast<envoy_filter_data_status_t>(Envoy::Http::FilterDataStatus::Continue);
const envoy_filter_data_status_t kEnvoyFilterDataStatusStopIterationAndBuffer =
    static_cast<envoy_filter_data_status_t>(Envoy::Http::FilterDataStatus::StopIterationAndBuffer);
const envoy_filter_data_status_t kEnvoyFilterDataStatusStopIterationNoBuffer =
    static_cast<envoy_filter_data_status_t>(Envoy::Http::FilterDataStatus::StopIterationNoBuffer);

const envoy_filter_trailers_status_t kEnvoyFilterTrailersStatusContinue =
    static_cast<envoy_filter_trailers_status_t>(Envoy::Http::FilterTrailersStatus::Continue);
const envoy_filter_trailers_status_t kEnvoyFilterTrailersStatusStopIteration =
    static_cast<envoy_filter_trailers_status_t>(Envoy::Http::FilterTrailersStatus::StopIteration);
