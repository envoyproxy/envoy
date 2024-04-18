Introduction
============

The Envoy xDS APIs are defined as `proto3
<https://developers.google.com/protocol-buffers/docs/proto3>`_ `Protocol Buffers
<https://developers.google.com/protocol-buffers/>`_ in the :repo:`api tree <api/>`. They
support:

* Streaming delivery of :ref:`xDS <xds_protocol>` API updates via gRPC. This reduces
  resource requirements and can lower the update latency.
* A new REST-JSON API in which the JSON/YAML formats are derived mechanically via the `proto3
  canonical JSON mapping
  <https://developers.google.com/protocol-buffers/docs/proto3#json>`_.
* Delivery of updates via the filesystem, REST-JSON or gRPC endpoints.
* Advanced load balancing through an extended endpoint assignment API and load
  and resource utilization reporting to management servers.
* :ref:`Stronger consistency and ordering properties
  <xds_protocol_eventual_consistency_considerations>`
  when needed. The APIs still maintain a baseline eventual consistency model.

See the :ref:`xDS protocol description <xds_protocol>` for
further details on aspects of xDS message exchange between Envoy and the management server.
