.. _config_http_conn_man_vhds:

Virtual Host Discovery Service (VHDS)
=====================================

The virtual host discovery service (VHDS) API is an optional API that Envoy
will call to dynamically fetch
:ref:`virtual hosts <envoy_api_msg_route.VirtualHost>`. A virtual host includes
a name and set of domains that get routed to it based on the incoming request's
host header.

* :ref:`v2 API reference <v2_grpc_streaming_endpoints>`

Statistics
----------

VHDS has a statistics tree rooted at *http.<stat_prefix>.vhds.<virtual_host_name>.*.
Any ``:`` character in the ``virtual_host_name`` name gets replaced with ``_`` in the
stats tree. The stats tree contains the following statistics:

.. csv-table::
  :header: Name, Type, Description
  :widths: 1, 1, 2

  config_reload, Counter, Total API fetches that resulted in a config reload due to a different config
  empty_update, Counter, Total count of empty updates received

