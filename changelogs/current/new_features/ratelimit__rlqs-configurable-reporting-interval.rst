Added :ref:`reporting_interval
<envoy_v3_api_field_extensions.filters.http.rate_limit_quota.v3.RateLimitQuotaFilterConfig.reporting_interval>`
to the Rate Limit Quota (RLQS) filter, allowing the quota usage reporting interval to be configured.
It previously defaulted to a hardcoded 5 seconds, which remains the default when the field is unset.
