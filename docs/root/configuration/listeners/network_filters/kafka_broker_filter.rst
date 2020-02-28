.. _config_network_filters_kafka_broker:

Kafka Broker filter
===================

The Apache Kafka broker filter decodes the client protocol for
`Apache Kafka <https://kafka.apache.org/>`_, both the requests and responses in the payload.
The message versions in `Kafka 2.4.0 <http://kafka.apache.org/24/protocol.html#protocol_api_keys>`_
are supported.
The filter attempts not to influence the communication between client and brokers, so the messages
that could not be decoded (due to Kafka client or broker running a newer version than supported by
this filter) are forwarded as-is.

* :ref:`v2 API reference <envoy_api_msg_config.filter.network.kafka_broker.v2alpha1.KafkaBroker>`
* This filter should be configured with the name *envoy.filters.network.kafka_broker*.

.. attention::

   The kafka_broker filter is experimental and is currently under active development.
   Capabilities will be expanded over time and the configuration structures are likely to change.

.. _config_network_filters_kafka_broker_config:

Configuration
-------------

The Kafka Broker filter should be chained with the TCP proxy filter as shown
in the configuration snippet below:

.. code-block:: yaml

  listeners:
  - address:
      socket_address:
        address: 127.0.0.1 # Host that Kafka clients should connect to.
        port_value: 19092  # Port that Kafka clients should connect to.
    filter_chains:
    - filters:
      - name: envoy.filters.network.kafka_broker
        typed_config:
          "@type": type.googleapis.com/envoy.config.filter.network.kafka_broker.v2alpha1.KafkaBroker
          stat_prefix: exampleprefix
      - name: envoy.filters.network.tcp_proxy
        typed_config:
          "@type": type.googleapis.com/envoy.config.filter.network.tcp_proxy.v2.TcpProxy
          stat_prefix: tcp
          cluster: localkafka
  clusters:
  - name: localkafka
    connect_timeout: 0.25s
    type: strict_dns
    lb_policy: round_robin
    hosts:
    - socket_address:
        address: 127.0.0.1 # Kafka broker's host.
        port_value: 9092   # Kafka broker's port.

The Kafka broker needs to advertise the Envoy listener port instead of its own.

.. code-block:: text

  # Listener value needs to be equal to cluster value in Envoy config
  # (will receive payloads from Envoy).
  listeners=PLAINTEXT://127.0.0.1:9092

  # Advertised listener value needs to be equal to Envoy's listener
  # (will make clients discovering this broker talk to it through Envoy).
  advertised.listeners=PLAINTEXT://127.0.0.1:19092

.. _config_network_filters_kafka_broker_stats:

Statistics
----------

Every configured Kafka Broker filter has statistics rooted at *kafka.<stat_prefix>.*, with multiple
statistics per message type.

.. csv-table::
  :header: Name, Type, Description
  :widths: 1, 1, 2

  request.TYPE, Counter, Number of times a request of particular type was received from Kafka client
  request.unknown, Counter, Number of times a request with format not recognized by this filter was received
  request.failure, Counter, Number of times a request with invalid format was received or other processing exception occurred
  response.TYPE, Counter, Number of times a response of particular type was received from Kafka broker
  response.TYPE_duration, Histogram, Response generation time in milliseconds
  response.unknown, Counter, Number of times a response with format not recognized by this filter was received
  response.failure, Counter, Number of times a response with invalid format was received or other processing exception occurred
