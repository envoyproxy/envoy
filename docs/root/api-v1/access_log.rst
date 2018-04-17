.. _config_access_log_v1:

Access logging
==============

Configuration
-------------

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

.. _config_access_log_path_param:

path
  *(required, string)* Path the access log is written to.

.. _config_access_log_format_param:

format
  *(optional, string)* Access log format. Envoy supports :ref:`custom access log formats
  <config_access_log_format>` as well as a :ref:`default format
  <config_access_log_default_format>`.

.. _config_access_log_filter_param:

filter
  *(optional, object)* :ref:`Filter <config_http_con_manager_access_log_filters_v1>` which is used
  to determine if the access log needs to be written.

.. _config_http_con_manager_access_log_filters_v1:

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
  *(optional, string)* Runtime key to get value for comparison. This value is used if defined.

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
  *(optional, string)* Runtime key to get value for comparison. This value is used if defined.


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


.. _config_http_con_manager_access_log_filters_runtime_v1:

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
