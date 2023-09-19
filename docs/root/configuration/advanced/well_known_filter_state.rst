.. _well_known_filter_state:

Well Known Filter State Objects
===============================

The following list of filter state objects are consumed by Envoy extensions:

.. list-table::
   :widths: auto
   :header-rows: 1
   :stub-columns: 1

   * - **Filter state key**
     - **Purpose**
   * - ``envoy.tcp_proxy.cluster``
     - | :ref:`TCP proxy <config_network_filters_tcp_proxy>` dynamic cluster name selection
       | on a per-connection basis.
       | Accepts a cluster name as a constructor.
   * - ``envoy.network.transport_socket.original_dst_address``
     - | :ref:`Original destination cluster <arch_overview_load_balancing_types_original_destination>` dynamic address selection.
       | Accepts an `IP:PORT` string as a constructor.
       | Fields:
       | - ``ip``: IP address value as a string;
       | - ``port``: port value as a number.
   * - ``envoy.upstream.dynamic_host``
     - | :ref:`Dynamic forward proxy <envoy_v3_api_msg_extensions.clusters.dynamic_forward_proxy.v3.ClusterConfig>`
       | upstream host override on a per-connection basis.
       | Accepts a host string as a constructor.
   * - ``envoy.upstream.dynamic_port``
     - | :ref:`Dynamic forward proxy <envoy_v3_api_msg_extensions.clusters.dynamic_forward_proxy.v3.ClusterConfig>`
       | upstream port override on a per-connection basis.
       | Accepts a port number string as a constructor.


The filter state object fields can be used in the format strings. For example,
the following format string references the port number in the original
destination cluster filter state object:

.. code-block:: none

  %FILTER_STATE(envoy.network.transport_socket.original_dst_address:FIELD:port)%
