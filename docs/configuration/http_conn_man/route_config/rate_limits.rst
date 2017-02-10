.. _config_http_conn_man_route_table_rate_limit_config:

Rate limit configuration
========================

Global rate limiting :ref:`architecture overview <arch_overview_rate_limit>`.

.. code-block:: json

  {
    "stage": "...",
    "kill_switch_key": "...",
    "actions": []
  }

stage
  *(optional, integer)* Refers to the stage set in the filter. If set, the rate limit configuration
  only applies to filters with the same stage number and for filters set to default. If not set,
  the rate limit configuration will apply for all rate limit filters set to default. The default
  value is 0.

  **NOTE:** This functionality hasn't been implemented yet and stage values are currently ignored.

kill_switch_key
  *(optional, string)* The key to be set in runtime to disable this rate limit configuration.

actions
  *(required, array)* A list of actions that are to be applied for this rate limit configuration.
  Order matters as the actions are processed sequentially and the descriptor will be composed by
  appending decriptor entries in that sequence.

.. _config_http_conn_man_route_table_rate_limit_actions:

Actions
-------

.. code-block:: json

  {
    "type": "..."
  }

type
  *(required, string)* The type of rate limit action to perform. The currently supported action
  types are *source_cluster*, *destination_cluster* , *request_headers*, *remote_address* and
  *route_key*.

Source Cluster
^^^^^^^^^^^^^^

.. code-block:: json

  {
    "type": "source_cluster"
  }

The following descriptor entry is appended to the descriptor:

  * ("from_cluster", "<local service cluster>")

<local service cluster> is derived from the :option:`--service-cluster` option.

Destination Cluster
^^^^^^^^^^^^^^^^^^^

.. code-block:: json

  {
    "type": "destination_cluster"
  }

The following descriptor entry is appended to the descriptor:

  * ("to_cluster", "<:ref:`route target cluster <config_http_conn_man_route_table_route_cluster>`>")

Request Headers
^^^^^^^^^^^^^^^

.. code-block:: json

  {
    "type": "request_headers",
    "header_name": "...",
    "descriptor_key" : "..."
  }

header_name
  *(required, string)* The header name to be queried from the request headers and used to
  populate the descriptor entry value for the *descriptor_key*.

descriptor_key
  *(required, string)* The key to use in the descriptor entry.

The following descriptor entry is appended when a header contains a key that matches the
*header_name*:

  * ("<descriptor_key>", "<header_value_queried_from_header>")

Remote Address
^^^^^^^^^^^^^^

.. code-block:: json

  {
    "type": "remote_address"
  }

The following descriptor entry is appended to the descriptor and is populated using the trusted
address from :ref:`x-forwarded-for <config_http_conn_man_headers_x-forwarded-for>`:

    * ("remote_address", "<:ref:`trusted address from x-forwarded-for <config_http_conn_man_headers_x-forwarded-for>`>")

Route Key
^^^^^^^^^

.. code-block:: json

  {
    "type": "route_key",
    "route_key" : "..."
  }


route_key
    *(required, string)* The value to use in the descriptor entry.

The following descriptor entry is appended to the descriptor:

    * ("route_key", "<route_key>")
