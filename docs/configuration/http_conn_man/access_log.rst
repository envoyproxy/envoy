.. _config_http_conn_man_access_log:

Access logging
==============

Configuration
-------------------------

Access logs are configured as part of the :ref:`HTTP connection manager config
<config_http_conn_man>`.

.. code-block:: json

  {
    "access_log": [
      {
        "path": "...",
        "format": "...",
        "filter": "{...}",
      },
    ]
  }

.. _config_http_conn_man_access_log_path_param:

path
  *(required, string)* Path the access log is written to.

.. _config_http_conn_man_access_log_format_param:

format
  *(optional, string)* Access log format. Envoy supports :ref:`custom access log formats
  <config_http_con_manager_access_log_format>` as well as a :ref:`default format
  <config_http_con_manager_access_log_default_format>`.

.. _config_http_conn_man_access_log_filter_param:

filter
  *(optional, object)* :ref:`Filter <config_http_con_manager_access_log_filters>` which is used to
  determine if the access log needs to be written.

.. _config_http_con_manager_access_log_format:

Format rules
------------

The access log format string contains either command operators or other characters interpreted as a
plain string. The access log formatter does not make any assumptions about a new line separator, so one
has to specified as part of the format string.
See the :ref:`default format <config_http_con_manager_access_log_default_format>` for an example.
Note that the access log line will contain a '-' character for every not set/empty value.

The following command operators are supported:

%START_TIME%
  Request start time including milliseconds.

%BYTES_RECEIVED%
  Body bytes received.

%PROTOCOL%
  Protocol. Currently either *HTTP/1.1* or *HTTP/2*.

%RESPONSE_CODE%
  HTTP response code. Note that a response code of '0' means that the server never sent the
  beginning of a response. This generally means that the (downstream) client disconnected.

%BYTES_SENT%
  Body bytes sent.

%DURATION%
  Total duration in milliseconds of the request from the start time to the last byte out.

%RESPONSE_FLAGS%
  Additional details about the response, if any. Possible values are:

  * **LH**: Local service failed :ref:`health check request <arch_overview_health_checking>` in addition to 503 response code.
  * **UH**: No healthy upstream hosts in upstream cluster in addition to 503 response code.
  * **UT**: Upstream request timeout in addition to 504 response code.
  * **LR**: Connection local reset in addition to 503 response code.
  * **UR**: Upstream remote reset in addition to 503 response code.
  * **UF**: Upstream connection failure in addition to 503 response code.
  * **UC**: Upstream connection termination in addition to 503 response code.
  * **UO**: Upstream overflow (:ref:`circuit breaking <arch_overview_circuit_break>`) in addition to 503 response code.
  * **NR**: No :ref:`route configured <arch_overview_http_routing>` for a given request in addition to 404 response code.
  * **DI**: The request processing was delayed for a period specified via :ref:`fault injection <config_http_filters_fault_injection>`.
  * **FI**: The request was aborted with a response code specified via :ref:`fault injection <config_http_filters_fault_injection>`.
  * **RL**: The request was ratelimited locally by the :ref:`HTTP rate limit filter <config_http_filters_rate_limit>` in addition to 429 response code.

%UPSTREAM_HOST%
  Upstream host URL (e.g., tcp://ip:port for TCP connections).

%UPSTREAM_CLUSTER%
  Upstream cluster to which the upstream host belongs to.

%REQ(X?Y):Z%
  An HTTP request header where X is the main HTTP header, Y is the alternative one, and Z is an
  optional parameter denoting string truncation up to Z characters long. The value is taken from the
  HTTP request header named X first and if it's not set, then request header Y is used. If none of
  the headers are present '-' symbol will be in the log.

%RESP(X?Y):Z%
  Same as **%REQ(X?Y):Z%** but taken from HTTP response headers.

.. _config_http_con_manager_access_log_default_format:

Default format
--------------

If custom format is not specified, Envoy uses the following default format:

.. code-block:: none

  [%START_TIME%] "%REQ(:METHOD)% %REQ(X-ENVOY-ORIGINAL-PATH?:PATH)% %PROTOCOL%"
  %RESPONSE_CODE% %RESPONSE_FLAGS% %BYTES_RECEIVED% %BYTES_SENT% %DURATION%
  %RESP(X-ENVOY-UPSTREAM-SERVICE-TIME)% "%REQ(X-FORWARDED-FOR)%" "%REQ(USER-AGENT)%"
  "%REQ(X-REQUEST-ID)%" "%REQ(:AUTHORITY)%" "%UPSTREAM_HOST%"\n

Example of the default Envoy access log format:

.. code-block:: none

  [2016-04-15T20:17:00.310Z] "POST /api/v1/locations HTTP/2" 204 - 154 0 226 100 "10.0.35.28"
  "nsq2http" "cc21d9b0-cf5c-432b-8c7e-98aeb7988cd2" "locations" "tcp://10.0.2.1:80"

.. _config_http_con_manager_access_log_filters:

Filters
-------

Envoy supports the following access log filters:

.. contents::
  :local:

Status code
^^^^^^^^^^^

.. code-block:: json

  {
    "filter": {
      "type": "status_code",
      "op": "...",
      "value": "...",
      "runtime_key": "..."
    }
  }

Filters on HTTP response/status code.

op
  *(required, string)* Comparison operator. Currently *>=*  and *=* are the only supported operators.

value
  *(required, integer)* Default value to compare against if runtime value is not available.

runtime_key
  *(optional, string)* Runtime key to get value for comparision. This value is used if defined.

Duration
^^^^^^^^

.. code-block:: json

  {
    "filter": {
      "type": "duration",
      "op": "..",
      "value": "...",
      "runtime_key": "..."
    }
  }

Filters on total request duration in milliseconds.

op
  *(required, string)* Comparison operator. Currently *>=* and *=* are the only supported operators.

value
  *(required, integer)* Default value to compare against if runtime values is not available.

runtime_key
  *(optional, string)* Runtime key to get value for comparision. This value is used if defined.


Not health check
^^^^^^^^^^^^^^^^

.. code-block:: json

  {
    "filter": {
      "type": "not_healthcheck"
    }
  }

Filters for requests that are not health check requests. A health check request is marked by
the :ref:`health check filter <config_http_filters_health_check>`.

Traceable
^^^^^^^^^

.. code-block:: json

  {
    "filter": {
      "type": "traceable_request"
    }
  }

Filters for requests that are traceable. See the :ref:`tracing overview <arch_overview_tracing>` for
more information on how a request becomes traceable.


.. _config_http_con_manager_access_log_filters_runtime:

Runtime
^^^^^^^^^
.. code-block:: json

  {
    "filter": {
      "type": "runtime",
      "key" : "..."
    }
  }

Filters for random sampling of requests. Sampling pivots on the header
:ref:`x-request-id<config_http_conn_man_headers_x-request-id>` being present. If
:ref:`x-request-id<config_http_conn_man_headers_x-request-id>` is present, the filter will
consistently sample across multiple hosts based on the runtime key value and the value extracted
from :ref:`x-request-id<config_http_conn_man_headers_x-request-id>`. If it is missing, the
filter will randomly sample based on the runtime key value.

key
  *(required, string)* Runtime key to get the percentage of requests to be sampled.
  This runtime control is specified in the range 0-100 and defaults to 0.

And
^^^

.. code-block:: json

  {
    "filter": {
      "type": "logical_and",
      "filters": []
    }
  }

Performs a logical "and" operation on the result of each filter in *filters*. Filters are evaluated
sequentially and if one of them returns false, the filter returns false immediately.

Or
^^

.. code-block:: json

  {
    "filter": {
      "type": "logical_or",
      "filters": []
    }
  }

Performs a logical "or" operation on the result of each individual filter. Filters are evaluated
sequentially and if one of them returns true, the filter returns true immediately.
