.. _config_network_filters_kafka_broker:

Kafka Broker filter
===================

The Apache Kafka broker filter decodes the client protocol for
`Apache Kafka <https://kafka.apache.org/>`_. It decodes the requests and responses in the payload.
The message versions in `Kafka 2.0 <http://kafka.apache.org/20/protocol.html#protocol_api_keys>`_
are supported.

.. attention::

   The kafka_broker filter is experimental and is currently under active development.
   Capabilities will be expanded over time and the configuration structures are likely to change.

.. _config_network_filters_kafka_broker_config:

Configuration
-------------

The Kafka Broker filter should be chained with the TCP proxy filter as shown
in the configuration snippet below:

.. code-block:: yaml

  filter_chains:
  - filters:
    - name: envoy.filters.network.kafka_broker
      config:
        stat_prefix: exampleprefix
    - name: envoy.tcp_proxy
      config:
        stat_prefix: tcp
        cluster: ...


.. _config_network_filters_kafka_broker_stats:

Statistics
----------

Every configured Kafka Broker filter has statistics rooted at *kafka.<stat_prefix>.*, with multiple
statistics per message type.

.. csv-table::
  :header: Name, Type, Description
  :widths: 1, 1, 2

  request.TYPE, Counter, Number of times a request of particular type was received from Kafka client
  request.failed_parse, Counter, Number of times a request could not be parsed
  response.TYPE, Counter, Number of times a response of particular type was received from Kafka broker
  response.TYPE_duration, Histogram, Response generation time in milliseconds
  response.failed_parse, Counter, Number of times a response could not be parsed
