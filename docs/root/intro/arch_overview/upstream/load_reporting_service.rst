.. _arch_overview_load_reporting_service:

Load Reporting Service (LRS)
============================

The Load Reporting Service provides a mechanism by which Envoy can emit Load Reports to a management
server at a regular cadence. Below is the envoy config to enable Load reporting service.

This will initiate a bi-directional stream with a management server. Upon connecting, the management
server can send a :ref:`LoadStatsResponse <envoy_v3_api_msg_service.load_stats.v3.LoadStatsResponse>`
to a node it is interested in getting the load reports for. Envoy in this node will start sending
:ref:`LoadStatsRequest <envoy_v3_api_msg_service.load_stats.v3.LoadStatsRequest>`. This is done periodically
based on the :ref:`load reporting interval <envoy_v3_api_field_service.load_stats.v3.LoadStatsResponse.load_reporting_interval>`

For details, take a look at the `sandbox example <https://www.envoyproxy.io/docs/envoy/latest/start/sandboxes/load_reporting_service.html>`_.

.. code-block:: yaml

    cluster_manager:
      load_stats_config:
        api_type: GRPC
        grpc_services:
          envoy_grpc:
            cluster_name: load_reporting_cluster
    clusters:
    - name: load_reporting_cluster
      connect_timeout: 0.25s
      type: strict_dns
      lb_policy: round_robin
      http2_protocol_options: {}
      load_assignment:
        cluster_name: load_reporting_cluster
        endpoints:
        - lb_endpoints:
          - endpoint:
              address:
                socket_address:
                  address: <ADDRESS>
                  port_value: <PORT>