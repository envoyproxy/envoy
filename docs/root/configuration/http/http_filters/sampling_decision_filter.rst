.. _config_http_filters_sampling_decision:

Sampling decision
=================

The sampling decision filter evaluates whether a request should be sampled based on different
criteria like runtime configuration, request ID, and independent randomness. It then stores the
decision in dynamic metadata. This allows access log filters and formatters to reference a
centralized sampling decision without re-evaluating the sampling logic.

* This filter should be configured with the type URL ``type.googleapis.com/envoy.extensions.filters.http.sampling_decision.v3.SamplingDecision``.
* :ref:`v3 API reference <envoy_v3_api_msg_extensions.filters.http.sampling_decision.v3.SamplingDecision>`

The filter evaluates sampling during request processing and stores the decision along with
sampling parameters in :ref:`dynamic metadata <envoy_v3_api_field_config.core.v3.Metadata>`.
This metadata can be consumed by access log filters to control logging, by formatters to include
sampling information in log output, or by other filters to make sampling-aware decisions.

By default, the filter uses deterministic sampling based on the request ID, ensuring that the
same request receives the same sampling decision across multiple evaluations. This is useful
when multiple access logs or filters need to make consistent sampling decisions. The filter
also supports independent randomness mode for scenarios where uncorrelated sampling decisions
are required.

Configuration
-------------

Basic Configuration
^^^^^^^^^^^^^^^^^^^

A simple configuration with 5% sampling:

.. code-block:: yaml

  http_filters:
    - name: envoy.filters.http.sampling_decision
      typed_config:
        "@type": type.googleapis.com/envoy.extensions.filters.http.sampling_decision.v3.SamplingDecision
        runtime_key: "sampling.requests"
        percent_sampled:
          numerator: 5
          denominator: HUNDRED

Access Log Integration
^^^^^^^^^^^^^^^^^^^^^^

Using the sampling decision to control access logging:

.. code-block:: yaml

  http_filters:
    - name: envoy.filters.http.sampling_decision
      typed_config:
        "@type": type.googleapis.com/envoy.extensions.filters.http.sampling_decision.v3.SamplingDecision
        runtime_key: "sampling.access_logs"
        percent_sampled:
          numerator: 10
          denominator: HUNDRED

  access_log:
    - filter:
        metadata_filter:
          matcher:
            filter: envoy.filters.http.sampling_decision
            path:
              - key: sampled
            value:
              bool_match: true
      typed_config:
        "@type": type.googleapis.com/envoy.extensions.access_loggers.file.v3.FileAccessLog
        path: /var/log/envoy/sampled_access.log
        log_format:
          text_format: |
            [%START_TIME%] sampled=%DYNAMIC_METADATA(envoy.filters.http.sampling_decision:sampled)% rate=%DYNAMIC_METADATA(envoy.filters.http.sampling_decision:numerator)%/%DYNAMIC_METADATA(envoy.filters.http.sampling_decision:denominator)% "%REQ(:METHOD)% %REQ(X-ENVOY-ORIGINAL-PATH?:PATH)% %PROTOCOL%" %RESPONSE_CODE% %RESPONSE_FLAGS% %BYTES_RECEIVED% %BYTES_SENT% %DURATION% %RESP(X-ENVOY-UPSTREAM-SERVICE-TIME)% "%REQ(X-FORWARDED-FOR)%" "%REQ(USER-AGENT)%" "%REQ(X-REQUEST-ID)%"

Custom Metadata Namespace
^^^^^^^^^^^^^^^^^^^^^^^^^^

Using a custom metadata namespace for multiple sampling decisions:

.. code-block:: yaml

  http_filters:
    - name: envoy.filters.http.sampling_decision
      typed_config:
        "@type": type.googleapis.com/envoy.extensions.filters.http.sampling_decision.v3.SamplingDecision
        runtime_key: "sampling.debug"
        percent_sampled:
          numerator: 1
          denominator: HUNDRED
        metadata_namespace: "sampling.debug"

    - name: envoy.filters.http.sampling_decision
      typed_config:
        "@type": type.googleapis.com/envoy.extensions.filters.http.sampling_decision.v3.SamplingDecision
        runtime_key: "sampling.metrics"
        percent_sampled:
          numerator: 50
          denominator: HUNDRED
        metadata_namespace: "sampling.metrics"

  access_log:
    - filter:
        metadata_filter:
          matcher:
            filter: sampling.debug
            path:
              - key: sampled
            value:
              bool_match: true
      typed_config:
        "@type": type.googleapis.com/envoy.extensions.access_loggers.file.v3.FileAccessLog
        path: /var/log/envoy/debug.log

    - filter:
        metadata_filter:
          matcher:
            filter: sampling.metrics
            path:
              - key: sampled
            value:
              bool_match: true
      typed_config:
        "@type": type.googleapis.com/envoy.extensions.access_loggers.file.v3.FileAccessLog
        path: /var/log/envoy/metrics.log

Runtime Override
^^^^^^^^^^^^^^^^

The sampling percentage can be overridden at runtime. Configuration:

.. code-block:: yaml

  http_filters:
    - name: envoy.filters.http.sampling_decision
      typed_config:
        "@type": type.googleapis.com/envoy.extensions.filters.http.sampling_decision.v3.SamplingDecision
        runtime_key: "sampling.requests"
        percent_sampled:
          numerator: 5
          denominator: HUNDRED

To override the sampling rate at runtime:

.. code-block:: console

  $ curl -X POST http://localhost:9901/runtime_modify?sampling.requests=10

This changes the sampling rate to 10% without requiring a configuration reload.

Independent Randomness
^^^^^^^^^^^^^^^^^^^^^^

Using independent randomness for uncorrelated sampling:

.. code-block:: yaml

  http_filters:
    - name: envoy.filters.http.sampling_decision
      typed_config:
        "@type": type.googleapis.com/envoy.extensions.filters.http.sampling_decision.v3.SamplingDecision
        runtime_key: "sampling.independent"
        percent_sampled:
          numerator: 5
          denominator: HUNDRED
        use_independent_randomness: true

.. note::

   When :ref:`use_independent_randomness <envoy_v3_api_field_extensions.filters.http.sampling_decision.v3.SamplingDecision.use_independent_randomness>`
   is set to true, the same request may receive different sampling decisions if the filter is
   evaluated multiple times. Use this setting carefully if consistent sampling behavior is required.

.. _config_http_filters_sampling_decision_dynamic_metadata:

Dynamic Metadata
----------------

The sampling decision filter emits dynamic metadata in the namespace specified by
:ref:`metadata_namespace <envoy_v3_api_field_extensions.filters.http.sampling_decision.v3.SamplingDecision.metadata_namespace>`
(defaults to ``envoy.filters.http.sampling_decision``). The following fields are set:

.. csv-table::
  :header: Name, Type, Description
  :widths: 1, 1, 2

  sampled, boolean, Whether the request was sampled based on the configured sampling rate
  numerator, number, The numerator of the sampling rate used (may be overridden by runtime)
  denominator, number, "The denominator of the sampling rate (100, 10000, or 1000000)"
  runtime_key, string, The runtime key used for sampling

The metadata can be accessed by access log filters using the
:ref:`metadata_filter <envoy_v3_api_msg_config.accesslog.v3.MetadataFilter>` or by formatters
using the ``%DYNAMIC_METADATA(...)%`` :ref:`command operator <config_access_log_format_dynamic_metadata>`.

Statistics
----------

The sampling decision filter does not emit any statistics.
