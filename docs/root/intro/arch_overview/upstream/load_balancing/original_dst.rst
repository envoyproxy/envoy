.. _arch_overview_load_balancing_types_original_destination:

Original destination
--------------------

This is a special purpose load balancer that can only be used with :ref:`an original destination
cluster <arch_overview_service_discovery_types_original_destination>`. Upstream host is selected
based on the downstream connection metadata, i.e., connections are opened to the same address as the
destination address of the incoming connection was before the connection was redirected to
Envoy. New destinations are added to the cluster by the load balancer on-demand, and the cluster
:ref:`periodically <envoy_v3_api_field_config.cluster.v3.Cluster.cleanup_interval>` cleans out unused hosts
from the cluster. No other :ref:`load balancing policy <envoy_v3_api_field_config.cluster.v3.Cluster.lb_policy>` can
be used with original destination clusters.

.. _arch_overview_load_balancing_types_original_destination_request_header:

Original destination host request header
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
Envoy can also pick up the original destination from a HTTP header called
:ref:`x-envoy-original-dst-host <config_http_conn_man_headers_x-envoy-original-dst-host>`.
Please note that fully resolved IP address should be passed in this header. For example if a request has to be
routed to a host with IP address 10.195.16.237 at port 8888, the request header value should be set as
``10.195.16.237:8888``.

.. _arch_overview_load_balancing_types_original_destination_request_header_filter_state:

Original destination filter state
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
Custom extensions can override the destination address using the filter state
object ``envoy.network.transport_socket.original_dst_address``. This behavior
can be used for tunneling to an intermediary proxy instead of the direct
original destination.
