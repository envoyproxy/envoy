Matcher input factories can declare when their data is available with ``availableAt()``. The
:ref:`HTTP <config_http_filters_rbac>` and :ref:`network <config_network_filters_rbac>` RBAC
filters now also accept an input whose data is available by the request headers or the connection,
respectively.
