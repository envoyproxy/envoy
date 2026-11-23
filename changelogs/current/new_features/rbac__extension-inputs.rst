Matcher input factories can declare, with ``availableAt()``, the stage by which their result is
final. The :ref:`HTTP <config_http_filters_rbac>`, :ref:`upstream <config_http_filters_upstream_rbac>`
and :ref:`network <config_network_filters_rbac>` RBAC filters now also accept an input outside their
built-in lists, but only if its factory declares a stage no later than the request headers (HTTP and
upstream) or the connection (network).
