.. _config_listener_filters:

Filters
=======

Network filter :ref:`architecture overview <arch_overview_network_filters>`.

.. code-block:: json

  {
    "type": "...",
    "name": "...",
    "config": "{...}"
  }

type
  *(required, string)* The type of filter to instantiate. Most filters implement a specific type,
  though it is theoretically possible for a filter to be written such that it can operate in
  multiple modes. Supported types are *read*, *write*, and *both*.

name
  *(required, string)* The name of the filter to instantiate. The name must match a :ref:`supported
  filter <config_network_filters>`.

config
  *(required, object)* Filter specific configuration which depends on the filter being instantiated.
  See the :ref:`supported filters <config_network_filters>` for further documentation.
