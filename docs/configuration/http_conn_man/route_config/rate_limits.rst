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
  *(optional, integer)* Refers to the stage set in the filter. If set, the rate limit configuration
  only applies to filters with the same stage number and for filters set to default. If not set,
  the rate limit configuration will apply for all rate limit filters set to default. The default
  value is 0.

  **NOTE:** This functionality hasn't been implemented yet and stage values are currently ignored.

disable_key
  *(optional, string)* The key to be set in runtime to disable this rate limit configuration.

actions
  *(required, array)* A list of actions that are to be applied for this rate limit configuration.
  Order matters as the actions are processed sequentially and the descriptor will be composed by
  appending decriptor entries in that sequence. See :ref:`composing actions
  <config_http_conn_man_route_table_rate_limit_composing_actions>` for additional documentation.

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

  * ("to_cluster", "<routed cluster>")

The routed cluster is chosen based on the :ref:`route table
<config_http_conn_man_route_table_route_cluster>` configuration for *cluster*, *weighted_clusters*
or *cluster_header*.

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

  * ("<descriptor_key>", "<header_value_queried_from_header>")

Remote Address
^^^^^^^^^^^^^^

.. code-block:: json

  {
    "type": "remote_address"
  }

The following descriptor entry is appended to the descriptor and is populated using the trusted
address from :ref:`x-forwarded-for <config_http_conn_man_headers_x-forwarded-for>`:

    * ("remote_address", "<:ref:`trusted address from x-forwarded-for
      <config_http_conn_man_headers_x-forwarded-for>`>")

Rate Limit Key
^^^^^^^^^^^^^^

.. code-block:: json

  {
    "type": "rate_limit_key",
    "rate_limit_value" : "..."
  }


rate_limit_value
    *(required, string)* The value to use in the descriptor entry.

The following descriptor entry is appended to the descriptor:

    * ("rate_limit_key", "<rate_limit_value>")


.. _config_http_conn_man_route_table_rate_limit_composing_actions:

Composing Actions
-----------------

Each action populates a descriptor entry. A vector of descriptor entries compose a descriptor. To
create more complex rate limit descriptors, actions can be composed in any order. The descriptor
will be populated in the order the actions are specified in the configuration.

For example, if you wanted the following descriptor:

  * ("rate_limit_key", "some_value"), ("source_cluster", "from_cluster")

The configuration would be:

.. code-block:: json

  {
    "actions" : [
      {
        "type" : "rate_limit_key",
        "rate_limit_value" : "some_value"
      },
      {
        "type" : "source_cluster"
      }
    ]
  }

If an action doesn't appened a descriptor entry, the next item in the action list will
be processed. For example given the following rate limit configuration, a request can
generate a few possible descriptors depending on what is present in the request.

.. code-block:: json

  {
    "actions" : [
      {
        "type" : "rate_limit_key",
        "rate_limit_value" : "some_value"
      },
      {
        "type" : "remote_address"
      },
      {
        "type" : "souce_cluster"
      }
    ]
  }

For a request with :ref:`x-forwarded-for<config_http_conn_man_headers_x-forwarded-for>` set and the
trusted address is for example *127.0.0.1*, the following descriptor would be generated:

  * ("rate_limit_key", "some_value"), ("remote_address", "127.0.0.1"), ("source_cluster",
    "from_cluster")

If a request did not set :ref:`x-forwarded-for<config_http_conn_man_headers_x-forwarded-for>`, the
following descriptor would be generated:

  * ("rate_limit_key", "some_value"), ("source_cluster", "from_cluster")
