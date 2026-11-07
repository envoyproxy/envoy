Added the ``envoy.filters.listener.original_dst.local_port`` filter state object to the
:ref:`original destination listener filter <config_listener_filters_original_dst>`. On sockets with an
IP address it replaces the destination port, for ports translated before the connection reaches
Envoy's network namespace, such as a Kubernetes Service mapping port 443 to a container port 10443.
