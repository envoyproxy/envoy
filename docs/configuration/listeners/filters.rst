.. _config_listener_filters:

Filters
=======

Network filter :ref:`architecture overview <arch_overview_network_filters>`.

.. code-block:: json

  {
    "name": "...",
    "config": "{...}"
  }

name
  *(required, string)* The name of the filter to instantiate. The name must match a :ref:`supported
  filter <config_network_filters>`.

config
  *(required, object)* Filter specific configuration which depends on the filter being instantiated.
  See the :ref:`supported filters <config_network_filters>` for further documentation.
