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

*Note: The fault injection filter must be inserted before any other filter,
including the router filter.*

.. code-block:: json

    {
      "type" : "decoder",
      "name" : "fault",
      "config" : {
        "abort" : {
          "abort_percent" : "...",
          "http_status" : "..."
        },
        "delay" : {
          "type" : "...",
          "fixed_delay_percent" : "...",
          "fixed_duration_ms" : "..."
        },
        "upstream_cluster" : "...",
        "headers" : []
      }
    }

abort.abort_percent
  *(required, integer)* The percentage of requests that
  should be aborted with the specified *http_status* code. Valid values
  range from 0 to 100.

abort.http_status
  *(required, integer)* The HTTP status code that will be used as the
  response code for the request being aborted.

delay.type:
  *(required, string)* Specifies the type of delay being
  injected. Currently only *fixed* delay type (step function) is supported.

delay.fixed_delay_percent:
  *(required, integer)* The percentage of requests that will
  be delayed for the duration specified by *fixed_duration_ms*. Valid
  values range from 0 to 100.

delay.fixed_duration_ms:
  *(required, integer)* The delay duration in
  milliseconds. Must be greater than 0.

upstream_cluster:
  *(optional, string)* Specifies the name of the (destination) upstream
  cluster that the filter should match on. Fault injection will be
  restricted to requests bound to the specific upstream cluster.

:ref:`headers <config_http_filters_fault_injection_headers>`
  *(optional, array)* Specifies a set of headers that the filter should match on.

The abort and delay blocks can be omitted. If they are not specified in the
configuration file, their respective values will be obtained from the
runtime.

Runtime
-------

The HTTP fault injection filter supports the following runtime settings:

http.fault.abort.abort_percent
  % of requests that will be aborted if the headers match. Defaults to the
  *abort_percent* specified in config. If the config does not contain an
  *abort* block, then *abort_percent* defaults to 0.

http.fault.abort.http_status
  HTTP status code that will be used as the  of requests that will be
  aborted if the headers match. Defaults to the HTTP status code specified
  in the config. If the config does not contain an *abort* block, then
  *http_status* defaults to 0.

http.fault.delay.fixed_delay_percent
  % of requests that will be delayed if the headers match. Defaults to the
  *delay_percent* specified in the config or 0 otherwise.

http.fault.delay.fixed_duration_ms
  The delay duration in milliseconds. If not specified, the
  *fixed_duration_ms* specified in the config will be used. If this field
  is missing from both the runtime and the config, no delays will be
  injected.

.. _config_http_filters_fault_injection_headers:

Headers
-------

The fault injection filter can be applied selectively to requests that
match a set of headers specified in the fault filter config. The chances of
actual fault injection further depend on the values of *abort_percent* and
*fixed_delay_percent* parameters. Each element of the array in the
*headers* field should be in the following format:

.. code-block:: json

  [
    {"name": "...", "value": "...", "regex": "..."}
  ]

name
  *(required, string)* Specifies the name of the header in the request.

value
  *(optional, string)* Specifies the value of the header. If the value is
  absent a request that has the *name* header will match, regardless of the
  header's value.

regex
  *(optional, boolean)* Specifies whether the header value is a regular expression
  or not. Defaults to false. The regex grammar used in the value field
  is defined `here <http://en.cppreference.com/w/cpp/regex/ecmascript>`_.

The filter will check the request's headers against all the specified
headers in the filter config. A match will happen if all the headers in the
config are present in the request with the same values (or based on
presence if the ``value`` field is not in the config).

Statistics
----------

The fault filter outputs statistics in the *http.<stat_prefix>.fault.* namespace. The :ref:`stat
prefix <config_http_conn_man_stat_prefix>` comes from the owning HTTP connection manager.

.. csv-table::
  :header: Name, Type, Description
  :widths: 1, 1, 2

  delays_injected, Counter, Total requests that were delayed
  aborts_injected, Counter, Total requests that were aborted

