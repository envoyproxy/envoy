.. _config_http_filters_rate_limit_quota:

Rate Limit Quota (Work-In-Progress)
===================================

.. attention::

  The Rate Limit Quota filter is currently under active development and is not ready for use yet.
  Capabilities and the configuration structures could be changed.

* Global rate limiting :ref:`architecture overview <arch_overview_global_rate_limit>`
* :ref:`v3 API reference <envoy_v3_api_msg_extensions.filters.http.rate_limit_quota.v3.RateLimitQuotaFilterConfig>`

This filter provides implementation of the global rate limit quota :ref:`protocol <envoy_v3_api_file_envoy/service/rate_limit_quota/v3/rlqs.proto>`.
The rate limit quota service (RLQS) provides quota assignments to each Envoy instance connected to the RLQS service. In addition to enforcing rate limit quota assignments,
this filter periodically reports request rates to the RLQS, allowing the RLQS server to rebalance quota assignments between Envoy instances based on the real-time
individual load of each Envoy instance. When quota assignments change, the RLQS proactively pushes the new assignment to Envoy.

The HTTP rate limit quota filter will call the rate limit quota service when it is configured in the HTTP connection manager filter chain. Filter configuration
defines the RLQS service and definitions of quota buckets that will receive quota assignments from the server. Quota buckets are defined by a set of matchers that determine
if a request is subject to the rate limit quota assigned to that bucket. Each matcher can contain multiple buckets by the means of the
:ref:`bucket_id_builder <envoy_v3_api_field_extensions.filters.http.rate_limit_quota.v3.RateLimitQuotaBucketSettings.bucket_id_builder>`. The bucket ID builder allows
quota buckets to be generated either dynamically based on request attributes, such as request header value or statically based on the configuration.

If a request does not match any set of matchers then quota assignment for the "catch all" bucket configured by the ``on_no_match`` field of the
:ref:`bucket_matchers <envoy_v3_api_field_extensions.filters.http.rate_limit_quota.v3.RateLimitQuotaFilterConfig.bucket_matchers>` is applied. If the ``on_no_match``
configuration is not provided, all unmatched requests are not rate limited (i.e. fail-open).

Bucket definitions can be overridden by the virtual host or route configurations. The more specific definition completely overrides the less specific definition.

Initially all Envoy's quota assignments are empty. The rate limit quota filter requests quota assignment from RLQS when the request matches to a bucket for the first time.
The behavior of the filter while it waits for the initial assignment is determined by the ``no_assignment_behavior`` value. In this state, requests can either all be
immediately allowed, denied until quota assignment is received.

A quota assignment may have an associated :ref:`time to live <envoy_v3_api_field_service.rate_limit_quota.v3.RateLimitQuotaResponse.BucketAction.QuotaAssignmentAction.assignment_time_to_live>`.
The RLQS is expected to update the assignment before the TTL runs out. If RLQS failed to update the assignment and its TTL
has expired, the filter can be configured to continue using the last quota assignment or fall back to a value predefined in the
:ref:`expired assignment configuration <envoy_v3_api_field_extensions.filters.http.rate_limit_quota.v3.RateLimitQuotaBucketSettings.expired_assignment_behavior>`.

The rate limit quota filter reports the request load for each bucket to the RLQS with the configured ``reporting_interval``. The RLQS may rebalance quota assignments based on the request
load that each Envoy receives and push new quota assignments to Envoy instances.

Failure modes
^^^^^^^^^^^^^

In case the connection to RLQS server fails, the filter will fall back to either the
:ref:`no assignment behavior <envoy_v3_api_field_extensions.filters.http.rate_limit_quota.v3.RateLimitQuotaBucketSettings.no_assignment_behavior>`
if it has not yet received a rate limit quota or to the
:ref:`expired assignment behavior <envoy_v3_api_field_extensions.filters.http.rate_limit_quota.v3.RateLimitQuotaBucketSettings.expired_assignment_behavior>` if
connection could not be re-established by the time the existing quota expired.

In case the RLQS client doesn't receive the initial bucket assignment (for any reason, including
RLQS server connection failures) within predetermined [1]_ time, such buckets will eventually be
purged from memory. Subsequent requests matched into the bucket will re-initialize the bucket
in the "no assignment" state, restarting the reports. This is explained in more details at
:ref:`Rate Limit Quota Service (RLQS) <envoy_v3_api_file_envoy/service/rate_limit_quota/v3/rlqs.proto>`.

.. [1] The exact time to wait for the initial assignment is chosen by the filter, and may vary based on the implementation.

Example 1
^^^^^^^^^

In this example the HTTP connection manager has the following bucket definitions in the rate limit quota filter
:ref:`configuration <envoy_v3_api_msg_extensions.filters.http.rate_limit_quota.v3.RateLimitQuotaFilterConfig>`. This
configuration enables a rate limit quota filter with 3 buckets. Note that bucket ID is a map of key-value pairs.

1.  Bucket id ``name: prod-rate-limit-quota`` for all requests with the ``deployment: prod`` header present. Until RLQS assigns a quota,
    all requests are allowed.

2.  Bucket id ``name: staging-rate-limit-quota`` for all requests with the ``deployment: staging`` header present. Until RLQS assigns a quota,
    all requests are denied.

3.  Bucket id ``name: default-rate-limit-quota`` for all other requests. Until RLQS assigns a quota, 1K RPS quota is applied.

.. literalinclude:: _include/rate-limit-quota-filter-configuration.yaml
   :language: yaml
   :lines: 28-96
   :linenos:
   :lineno-start: 28
   :caption: :download:`rate-limit-quota-filter-configuration.yaml <_include/rate-limit-quota-filter-configuration.yaml>`

Rate Limit Quota Override
-------------------------

Rate limit filter :ref:`configuration <envoy_v3_api_msg_extensions.filters.http.rate_limit_quota.v3.RateLimitQuotaFilterConfig>` can be overridden
at the virtual host or route levels using the :ref:`RateLimitQuotaOverride <envoy_v3_api_msg_extensions.filters.http.rate_limit_quota.v3.RateLimitQuotaOverride>`
configuration. The more specific configuration fully overrides less specific configuration.

Matcher extensions
------------------

TODO

Statistics
----------

The rate limit filter outputs statistics in the ``cluster.<route target cluster>.rate_limit_quota.`` namespace.
429 responses or the configured
:ref:`rate limited status <envoy_v3_api_field_extensions.filters.http.rate_limit_quota.v3.RateLimitQuotaBucketSettings.DenyResponseSettings.http_status>`
are emitted to the normal cluster :ref:`dynamic HTTP statistics <config_cluster_manager_cluster_stats_dynamic_http>`.

.. csv-table::
  :header: Name, Type, Description
  :widths: 1, 1, 2

  buckets, Counter, Total number of quota buckets created
  assignments, Counter, Total rate limit assignments received from the rate limit quota service
  error, Counter, Total errors contacting the rate limit quota service
  over_limit, Counter, Total requests that exceeded assigned rate limit
  no_assigment, Counter, Total requests that were applied the :ref:`no_assigment_behavior <envoy_v3_api_field_extensions.filters.http.rate_limit_quota.v3.RateLimitQuotaBucketSettings.no_assignment_behavior>`
  expired_assigment, Counter, Total requests that were applied the :ref:`expired_assignment_behavior <envoy_v3_api_field_extensions.filters.http.rate_limit_quota.v3.RateLimitQuotaBucketSettings.expired_assignment_behavior>`
