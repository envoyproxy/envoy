.. _config_stat_sinks_kafka:

Kafka Stat Sink
===============

The :ref:`KafkaStatsSinkConfig <envoy_v3_api_msg_extensions.stat_sinks.kafka.v3.KafkaStatsSinkConfig>`
configuration specifies a stat sink that produces metrics directly to an
`Apache Kafka <https://kafka.apache.org/>`_ topic using
`librdkafka <https://github.com/confluentinc/librdkafka>`_.

This sink is useful in high-scale deployments where sending metrics through a gRPC
``metrics_service`` collector introduces unacceptable memory pressure. By producing
directly to Kafka, intermediate collector infrastructure can be eliminated.

* This extension should be configured with the type URL
  ``type.googleapis.com/envoy.extensions.stat_sinks.kafka.v3.KafkaStatsSinkConfig``.
* :ref:`v3 API reference <envoy_v3_api_msg_extensions.stat_sinks.kafka.v3.KafkaStatsSinkConfig>`

.. attention::

   The Kafka stat sink is only included in :ref:`contrib images <install_contrib>`

.. attention::

   The Kafka stat sink is experimental and is currently under active development.
   Capabilities will be expanded over time and the configuration structures are likely to change.

Serialization formats
---------------------

Two serialization formats are supported:

**JSON** (default)
  Each Kafka message value is a JSON object containing an array of metric entries.
  This is human-readable and easy to consume with standard Kafka tooling.

**PROTOBUF**
  Each Kafka message value is a binary-serialized
  ``envoy.service.metrics.v3.StreamMetricsMessage`` containing
  ``io.prometheus.client.MetricFamily`` entries. This is the same wire format used by
  the gRPC :ref:`metrics_service <envoy_v3_api_msg_config.metrics.v3.MetricsServiceConfig>` sink,
  allowing consumers to reuse existing Protobuf deserializers.

Configuration example
---------------------

.. code-block:: yaml

  stats_flush_interval: 10s

  stats_sinks:
    - name: envoy.stat_sinks.kafka
      typed_config:
        "@type": type.googleapis.com/envoy.extensions.stat_sinks.kafka.v3.KafkaStatsSinkConfig
        broker_list: "kafka1:9092,kafka2:9092"
        topic: "envoy-metrics"
        batch_size: 100
        format: PROTOBUF
        emit_tags_as_labels: true
        report_counters_as_deltas: true
        buffer_flush_timeout_ms: 500

Authentication
--------------

Authentication and encryption are configured through the ``producer_config`` map, which
passes key-value pairs directly to librdkafka. For example, to use SASL/SCRAM with TLS:

.. code-block:: yaml

  stats_sinks:
    - name: envoy.stat_sinks.kafka
      typed_config:
        "@type": type.googleapis.com/envoy.extensions.stat_sinks.kafka.v3.KafkaStatsSinkConfig
        broker_list: "kafka:9093"
        topic: "envoy-metrics"
        format: PROTOBUF
        producer_config:
          security.protocol: "SASL_SSL"
          sasl.mechanism: "SCRAM-SHA-256"
          sasl.username: "envoy"
          sasl.password: "secret"
          ssl.ca.location: "/etc/ssl/certs/ca.pem"

See the `librdkafka configuration reference <https://github.com/confluentinc/librdkafka/blob/master/CONFIGURATION.md>`_
for the full list of supported properties.
