.. _config_http_conn_man_route_table_rate_limit_config:

Rate limit configuration
========================

Global rate limiting :ref:`architecture overview <arch_overview_rate_limit>`.

.. code-block:: json

  {
    "stage": "...",
    "disable_key": "...",
    "actions": []
  }

stage
  *(optional, integer)* Refers to the stage set in the filter. The rate limit configuration
  only applies to filters with the same stage number. The default stage number is 0.

  **NOTE:** The filter supports a range of 0 - 10 inclusively for stage numbers.

disable_key
  *(optional, string)* The key to be set in runtime to disable this rate limit configuration.

actions
  *(required, array)* A list of actions that are to be applied for this rate limit configuration.
  Order matters as the actions are processed sequentially and the descriptor is composed by
  appending descriptor entries in that sequence. If an action cannot append a descriptor entry,
  no descriptor is generated for the configuration. See :ref:`composing actions
  <config_http_filters_rate_limit_composing_actions>` for additional documentation.

.. _config_http_conn_man_route_table_rate_limit_actions:

Actions
-------

.. code-block:: json

  {
    "type": "..."
  }

type
  *(required, string)* The type of rate limit action to perform. The currently supported action
  types are *source_cluster*, *destination_cluster* , *request_headers*, *remote_address*,
  *generic_key* and *header_value_match*.

Source Cluster
^^^^^^^^^^^^^^

.. code-block:: json

  {
    "type": "source_cluster"
  }

The following descriptor entry is appended to the descriptor:

.. code-block:: cpp

  ("source_cluster", "<local service cluster>")

<local service cluster> is derived from the :option:`--service-cluster` option.

Destination Cluster
^^^^^^^^^^^^^^^^^^^

.. code-block:: json

  {
    "type": "destination_cluster"
  }

The following descriptor entry is appended to the descriptor:

.. code-block:: cpp

  ("destination_cluster", "<routed target cluster>")

Once a request matches against a route table rule, a routed cluster is determined by one of the
following :ref:`route table configuration <config_http_conn_man_route_table_route_cluster>`
settings:

  * :ref:`cluster <config_http_conn_man_route_table_route_cluster>` indicates the upstream cluster
    to route to.
  * :ref:`weighted_clusters <config_http_conn_man_route_table_route_config_weighted_clusters>`
    chooses a cluster randomly from a set of clusters with attributed weight.
  * :ref:`cluster_header<config_http_conn_man_route_table_route_cluster_header>` indicates which
    header in the request contains the target cluster.

Request Headers
^^^^^^^^^^^^^^^

.. code-block:: json

  {
    "type": "request_headers",
    "header_name": "...",
    "descriptor_key" : "..."
  }

header_name
  *(required, string)* The header name to be queried from the request headers. The header's value is
  used to populate the value of the descriptor entry for the descriptor_key.

descriptor_key
  *(required, string)* The key to use in the descriptor entry.

The following descriptor entry is appended when a header contains a key that matches the
*header_name*:

.. code-block:: cpp

  ("<descriptor_key>", "<header_value_queried_from_header>")

Remote Address
^^^^^^^^^^^^^^

.. code-block:: json

  {
    "type": "remote_address"
  }

The following descriptor entry is appended to the descriptor and is populated using the trusted
address from :ref:`x-forwarded-for <config_http_conn_man_headers_x-forwarded-for>`:

.. code-block:: cpp

  ("remote_address", "<trusted address from x-forwarded-for>")

Generic Key
^^^^^^^^^^^

.. code-block:: json

  {
    "type": "generic_key",
    "descriptor_value" : "..."
  }


descriptor_value
  *(required, string)* The value to use in the descriptor entry.

The following descriptor entry is appended to the descriptor:

.. code-block:: cpp

  ("generic_key", "<descriptor_value>")

Header Value Match
^^^^^^^^^^^^^^^^^^

.. code-block:: json

  {
    "type": "header_value_match",
    "descriptor_value" : "...",
    "expect_match" : "...",
    "headers" : []
  }


descriptor_value
  *(required, string)* The value to use in the descriptor entry.

expect_match
  *(optional, boolean)* If set to true, the action will append a descriptor entry when the request
  matches the :ref:`headers<config_http_conn_man_route_table_route_headers>`. If set to false,
  the action will append a descriptor entry when the request does not match the
  :ref:`headers<config_http_conn_man_route_table_route_headers>`. The default value is true.

:ref:`headers<config_http_conn_man_route_table_route_headers>`
  *(required, array)* Specifies a set of headers that the rate limit action should match on. The
  action will check the request's headers against all the specified headers in the config. A match
  will happen if all the headers in the config are present in the request with the same values (or
  based on presence if the ``value`` field is not in the config).

The following descriptor entry is appended to the descriptor:
.. code-block:: cpp

  ("header_match", "<descriptor_value>")
