.. _config_http_conn_man_filters:

Filters
=======

HTTP filter :ref:`architecture overview <arch_overview_http_filters>`.

.. code-block:: json

  {
    "name": "...",
    "config": "{...}"
  }

name
  *(required, string)* The name of the filter to instantiate. The name must match a :ref:`supported
  filter <config_http_filters>`.

config
  *(required, object)*  Filter specific configuration which depends on the filter being
  instantiated. See the :ref:`supported filters <config_http_filters>` for further documentation.
