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

Currently, the fault injection filter has the following limitations:

* Abort codes are restricted to HTTP status codes only
* Delays are restricted to fixed duration.

Future versions will include support for restricting faults to specific
routes, injecting *gRPC* and *HTTP/2* specific error codes and delay
durations based on distributions.

Configuration
-------------

.. note::

  The fault injection filter must be inserted before any other filter,
  including the router filter.

* :ref:`v1 API reference <config_http_filters_fault_injection_v1>`
* :ref:`v2 API reference <envoy_api_msg_config.filter.http.fault.v2.HTTPFault>`

Runtime
-------

The HTTP fault injection filter supports the following global runtime settings:

fault.http.abort.abort_percent
  % of requests that will be aborted if the headers match. Defaults to the
  *abort_percent* specified in config. If the config does not contain an
  *abort* block, then *abort_percent* defaults to 0.

fault.http.abort.http_status
  HTTP status code that will be used as the  of requests that will be
  aborted if the headers match. Defaults to the HTTP status code specified
  in the config. If the config does not contain an *abort* block, then
  *http_status* defaults to 0.

fault.http.delay.fixed_delay_percent
  % of requests that will be delayed if the headers match. Defaults to the
  *delay_percent* specified in the config or 0 otherwise.

fault.http.delay.fixed_duration_ms
  The delay duration in milliseconds. If not specified, the
  *fixed_duration_ms* specified in the config will be used. If this field
  is missing from both the runtime and the config, no delays will be
  injected.

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
  <downstream-cluster>.delays_injected, Counter, Total delayed requests for the given downstream cluster
  <downstream-cluster>.aborts_injected, Counter, Total aborted requests for the given downstream cluster
