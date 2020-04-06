.. _config_http_filters_fault_injection:

Fault Injection
===============

The fault injection filter can be used to test the resiliency of
microservices to different forms of failures. The filter can be used to
inject delays and abort requests with user-specified error codes, thereby
providing the ability to stage different failure scenarios such as service
failures, service overloads, high network latency, network partitions,
etc. Faults injection can be limited to a specific set of requests based on
the (destination) upstream cluster of a request and/or a set of pre-defined
request headers.

The scope of failures is restricted to those that are observable by an
application communicating over the network. CPU and disk failures on the
local host cannot be emulated.

Configuration
-------------

.. note::

  The fault injection filter must be inserted before any other filter,
  including the router filter.

* :ref:`v2 API reference <envoy_api_msg_config.filter.http.fault.v2.HTTPFault>`
* This filter should be configured with the name *envoy.filters.http.fault*.

.. _config_http_filters_fault_injection_http_header:

Controlling fault injection via HTTP headers
--------------------------------------------

The fault filter has the capability to allow fault configuration to be specified by the caller.
This is useful in certain scenarios in which it is desired to allow the client to specify its own
fault configuration. The currently supported header controls are:

* Request abort configuration via the *x-envoy-fault-abort-request* header. The header value
  should be an integer that specifies the HTTP status code to return in response to a request
  and must be in the range [200, 600). In order for the header to work, :ref:`header_abort
  <envoy_api_field_config.filter.http.fault.v2.FaultAbort.header_abort>` needs to be set.
* Request delay configuration via the *x-envoy-fault-delay-request* header. The header value
  should be an integer that specifies the number of milliseconds to throttle the latency for.
  In order for the header to work, :ref:`header_delay
  <envoy_api_field_config.filter.fault.v2.FaultDelay.header_delay>` needs to be set.
* Response rate limit configuration via the *x-envoy-fault-throughput-response* header. The
  header value should be an integer that specifies the limit in KiB/s and must be > 0. In order
  for the header to work, :ref:`header_limit
  <envoy_api_field_config.filter.fault.v2.FaultRateLimit.header_limit>` needs to be set.

.. attention::

  Allowing header control is inherently dangerous if exposed to untrusted clients. In this case,
  it is suggested to use the :ref:`max_active_faults
  <envoy_api_field_config.filter.http.fault.v2.HTTPFault.max_active_faults>` setting to limit the
  maximum concurrent faults that can be active at any given time.

The following is an example configuration that enables header control for both of the above
options:

.. code-block:: yaml

  name: envoy.filters.http.fault
  typed_config:
    "@type": type.googleapis.com/envoy.config.filter.http.fault.v2.HTTPFault
    max_active_faults: 100
    abort:
      header_abort: {}
      percentage:
        numerator: 100
    delay:
      header_delay: {}
      percentage:
        numerator: 100
    response_rate_limit:
      header_limit: {}
      percentage:
        numerator: 100

.. _config_http_filters_fault_injection_runtime:

Runtime
-------

The HTTP fault injection filter supports the following global runtime settings:

.. attention::

  Some of the following runtime keys require the filter to be configured for the specific fault
  type and some do not. Please consult the documentation for each key for more information.

fault.http.abort.abort_percent
  % of requests that will be aborted if the headers match. Defaults to the
  *abort_percent* specified in config. If the config does not contain an
  *abort* block, then *abort_percent* defaults to 0. For historic reasons, this runtime key is
  available regardless of whether the filter is :ref:`configured for abort
  <envoy_api_field_config.filter.http.fault.v2.HTTPFault.abort>`.

fault.http.abort.http_status
  HTTP status code that will be used as the  of requests that will be
  aborted if the headers match. Defaults to the HTTP status code specified
  in the config. If the config does not contain an *abort* block, then
  *http_status* defaults to 0. For historic reasons, this runtime key is
  available regardless of whether the filter is :ref:`configured for abort
  <envoy_api_field_config.filter.http.fault.v2.HTTPFault.abort>`.

fault.http.delay.fixed_delay_percent
  % of requests that will be delayed if the headers match. Defaults to the
  *delay_percent* specified in the config or 0 otherwise. This runtime key is only available when
  the filter is :ref:`configured for delay
  <envoy_api_field_config.filter.http.fault.v2.HTTPFault.delay>`.

fault.http.delay.fixed_duration_ms
  The delay duration in milliseconds. If not specified, the
  *fixed_duration_ms* specified in the config will be used. If this field
  is missing from both the runtime and the config, no delays will be
  injected. This runtime key is only available when the filter is :ref:`configured for delay
  <envoy_api_field_config.filter.http.fault.v2.HTTPFault.delay>`.

fault.http.max_active_faults
  The maximum number of active faults (of all types) that Envoy will will inject via the fault
  filter. This can be used in cases where it is desired that faults are 100% injected,
  but the user wants to avoid a situation in which too many unexpected concurrent faulting requests
  cause resource constraint issues. If not specified, the :ref:`max_active_faults
  <envoy_api_field_config.filter.http.fault.v2.HTTPFault.max_active_faults>` setting will be used.

fault.http.rate_limit.response_percent
  % of requests which will have a response rate limit fault injected. Defaults to the value set in
  the :ref:`percentage <envoy_api_field_config.filter.fault.v2.FaultRateLimit.percentage>` field.
  This runtime key is only available when the filter is :ref:`configured for response rate limiting
  <envoy_api_field_config.filter.http.fault.v2.HTTPFault.response_rate_limit>`.

*Note*, fault filter runtime settings for the specific downstream cluster
override the default ones if present. The following are downstream specific
runtime keys:

* fault.http.<downstream-cluster>.abort.abort_percent
* fault.http.<downstream-cluster>.abort.http_status
* fault.http.<downstream-cluster>.delay.fixed_delay_percent
* fault.http.<downstream-cluster>.delay.fixed_duration_ms

Downstream cluster name is taken from
:ref:`the HTTP x-envoy-downstream-service-cluster <config_http_conn_man_headers_downstream-service-cluster>`
header. If the following settings are not found in the runtime it defaults to the global runtime settings
which defaults to the config settings.

.. _config_http_filters_fault_injection_stats:

Statistics
----------

The fault filter outputs statistics in the *http.<stat_prefix>.fault.* namespace. The :ref:`stat prefix
<envoy_api_field_config.filter.network.http_connection_manager.v2.HttpConnectionManager.stat_prefix>` comes from the
owning HTTP connection manager.

.. csv-table::
  :header: Name, Type, Description
  :widths: 1, 1, 2

  delays_injected, Counter, Total requests that were delayed
  aborts_injected, Counter, Total requests that were aborted
  response_rl_injected, Counter, "Total requests that had a response rate limit selected for injection (actually injection may not occur due to disconnect, reset, no body, etc.)"
  faults_overflow, Counter, Total number of faults that were not injected due to overflowing the :ref:`max_active_faults <envoy_api_field_config.filter.http.fault.v2.HTTPFault.max_active_faults>` setting
  active_faults, Gauge, Total number of faults active at the current time
  <downstream-cluster>.delays_injected, Counter, Total delayed requests for the given downstream cluster
  <downstream-cluster>.aborts_injected, Counter, Total aborted requests for the given downstream cluster
