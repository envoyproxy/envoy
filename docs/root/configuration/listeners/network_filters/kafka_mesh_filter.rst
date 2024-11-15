.. _config_network_filters_kafka_mesh:

Kafka Mesh filter
===================

The Apache Kafka mesh filter provides a facade for `Apache Kafka <https://kafka.apache.org/>`_
clusters.

It allows for processing of Produce (producer) and Fetch (consumer) requests sent by downstream
clients.

The requests received by this filter instance can be forwarded to one of multiple clusters,
depending on the configured forwarding rules.

Corresponding message versions from Kafka 3.8.0 are supported.

* This filter should be configured with the type URL ``type.googleapis.com/envoy.extensions.filters.network.kafka_mesh.v3alpha.KafkaMesh``.
* :ref:`v3 API reference <envoy_v3_api_msg_extensions.filters.network.kafka_mesh.v3alpha.KafkaMesh>`

.. attention::

   The Kafka mesh filter is only included in :ref:`contrib images <install_contrib>`

.. attention::

   The kafka_mesh filter is experimental and is currently under active development.
   Capabilities will be expanded over time and the configuration structures are likely to change.

.. attention::

   The kafka_mesh filter is does not work on Windows (the blocker is getting librdkafka compiled).

.. _config_network_filters_kafka_mesh_config:

Configuration
-------------

Below example shows us typical filter configuration that proxies 3 Kafka clusters.
Clients are going to connect to '127.0.0.1:19092', and their messages are going to be distributed
to cluster depending on topic names.

.. code-block:: yaml

  listeners:
  - address:
      socket_address:
        address: 127.0.0.1 # Host that Kafka clients should connect to.
        port_value: 19092  # Port that Kafka clients should connect to.
    filter_chains:
    - filters:
      - name: envoy.filters.network.kafka_mesh
        typed_config:
          "@type": type.googleapis.com/envoy.extensions.filters.network.kafka_mesh.v3alpha.KafkaMesh
          advertised_host: "127.0.0.1"
          advertised_port: 19092
          upstream_clusters:
          - cluster_name: kafka_c1
            bootstrap_servers: cluster1_node1:9092,cluster1_node2:9092,cluster1_node3:9092
            partition_count: 1
          - cluster_name: kafka_c2
            bootstrap_servers: cluster2_node1:9092,cluster2_node2:9092,cluster2_node3:9092
            partition_count: 1
          - cluster_name: kafka_c3
            bootstrap_servers: cluster3_node1:9092,cluster3_node2:9092
            partition_count: 5
            producer_config:
              acks: "1"
              linger.ms: "500"
            consumer_config:
              client.id: "my-envoy-consumer"
          forwarding_rules:
          - target_cluster: kafka_c1
            topic_prefix: apples
          - target_cluster: kafka_c2
            topic_prefix: bananas
          - target_cluster: kafka_c3
            topic_prefix: cherries

It should be noted that Kafka broker filter can be inserted before Kafka mesh filter in the filter
chain to capture the request processing metrics.

.. _config_network_filters_kafka_mesh_notes:

Notes
-----

#. The records are being sent/received using embedded
   `librdkafka <https://github.com/confluentinc/librdkafka>`_ producers/consumers.
#. librdkafka was compiled without ssl, lz4, gssapi, so related custom config options are
   not supported.
#. Invalid custom configs are not found at startup (only when appropriate producers or consumers
   are being initialised). Requests that would have referenced these clusters are going to close
   connection and fail.
#. Requests that reference to topics that do not match any of the rules are going to close
   connection and fail. This usually should not happen (clients request metadata first, and they
   should then fail with 'no broker available' first), but is possible if someone tailors binary
   payloads over the connection.

Producer proxy
--------------

#. The embedded librdkafka producers that are pointing at upstream Kafka clusters are created
   per Envoy worker thread (so the throughput can be increased with `--concurrency` option,
   allowing for requests to be processed by a larger number of producers).
#. Only ProduceRequests with version 2 are supported (what means very old producers like 0.8
   are not going to be supported).
#. Python producers need to set API version of at least 1.0.0, so that the produce requests
   they send are going to have records with magic equal to 2.
#. Downstream handling of Kafka producer 'acks' property is delegated to upstream client.
   E.g. if upstream client is configured to use acks=0 then the response is going to be sent
   to downstream client as soon as possible (even if they had non-zero acks!).
#. As the filter splits single producer requests into separate records, it's possible that delivery
   of only some of these records fails. In that case, the response returned to upstream client is
   a failure, however it is possible some of the records have been appended in the target cluster.
#. Because of the splitting mentioned above, records are not necessarily appended one after another
   (as they do not get sent as single request to upstream). Users that want to avoid this scenario
   might want to take a look into downstream producer configs: 'linger.ms' and 'batch.size'.

Consumer proxy
--------------

#. Currently the consumer proxy supports only stateful proxying - Envoy uses upstream-pointing
   librdkafka consumers to receive the records, and does that only when more data is requested.
#. Users might want to take a look consumers' config property *group.id* to manage the consumers'
   offset committing behaviour (what is meaningful across Envoy restarts).
#. When requesting consumer position, the response always contains offset = 0
   (see *list_offsets.cc*).
#. Record offset information is provided, but record batch offset delta is not -
   it has been observed that the Apache Kafka Java client is not going to update its position
   despite receiving records (see *fetch_record_converter.cc*).
#. The Fetch response is sent downstream if it has collected at least 3 records (see *fetch.cc*).
   The data about requested bytes etc. in the request is ignored by the current implementation.
#. The Fetch response is sent after it is considered to be fulfilled (see above) or the hardcoded
   timeout of 5 seconds passes (see *fetch.cc*). Timeout specified by request is ignored.
#. The consumers are going to poll records from topics as long as there are incoming requests for
   these topics, without considering partitions.
   Users are encouraged to make sure all partitions are being consumed from to avoid a situation
   when e.g. we are only fetching records from partition 0, but the proxy receives records
   for partition 0 (which are sent downstream) and partition 1 (which are kept in memory until
   someone shows interest in them).
