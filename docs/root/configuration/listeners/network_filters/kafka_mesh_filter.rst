.. _config_network_filters_kafka_mesh:

Kafka Mesh filter
===================

The Apache Kafka mesh filter TODO

.. attention::

   The kafka_mesh filter is experimental and is currently under active development.
   Capabilities will be expanded over time and the configuration structures are likely to change.

.. _config_network_filters_kafka_mesh_config:

Configuration
-------------

The Kafka Mesh filter TODO

.. code-block:: yaml

  listeners:
  - address:
      socket_address:
        address: 127.0.0.1 # Host that Kafka clients should connect to.
        port_value: 19092  # Port that Kafka clients should connect to.
    filter_chains:
    - filters:
      - name: envoy.filters.network.kafka_mesh
        config:
          stat_prefix: exampleprefix

.. _config_network_filters_kafka_mesh_stats:

Statistics
----------

Every configured Kafka Mesh filter has statistics rooted at *kafka.<stat_prefix>.*, with multiple
statistics per message type. TODO

.. csv-table::
  :header: Name, Type, Description
  :widths: 1, 1, 2

  request.TYPE, Counter, TODO
