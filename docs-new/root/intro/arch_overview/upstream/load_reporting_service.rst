.. _arch_overview_load_reporting_service:

Load Reporting Service (LRS)
============================

The Load Reporting Service provides a mechanism by which Envoy can emit Load Reports to a management
server at a regular cadence.

This will initiate a bi-directional stream with a management server. Upon connecting, the management
server can send a :ref:`LoadStatsResponse <envoy_v3_api_msg_service.load_stats.v3.LoadStatsResponse>`
to a node it is interested in getting the load reports for. Envoy in this node will start sending
:ref:`LoadStatsRequest <envoy_v3_api_msg_service.load_stats.v3.LoadStatsRequest>`. This is done periodically
based on the :ref:`load reporting interval <envoy_v3_api_field_service.load_stats.v3.LoadStatsResponse.load_reporting_interval>`.

Example of an Envoy config with LRS:

.. literalinclude:: /start/sandboxes/_include/load-reporting-service/envoy.yaml
    :language: yaml
    :linenos:
    :emphasize-lines: 60-64
    :caption: :download:`envoy.yaml </start/sandboxes/_include/load-reporting-service/envoy.yaml>`
