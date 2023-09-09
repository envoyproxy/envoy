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
     - :ref:`TCP proxy <config_network_filters_tcp_proxy>` dynamic cluster selection on a per-connection basis.
   * - ``envoy.network.transport_socket.original_dst_address``
     - | :ref:`Original destination cluster <arch_overview_load_balancing_types_original_destination>` dynamic address selection.
       | Fields:
       | - *ip*: IP address value;
       | - *port*: port value.
