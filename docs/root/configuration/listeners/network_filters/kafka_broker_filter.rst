.. _config_network_filters_kafka_broker:

Kafka Broker filter
===================

The Apache Kafka broker filter decodes the client protocol for
`Apache Kafka <https://kafka.apache.org/>`_, both the requests and responses in the payload.
The message versions in `Kafka 3.5.1 <http://kafka.apache.org/35/protocol.html#protocol_api_keys>`_
are supported (apart from ConsumerGroupHeartbeat).

By default the filter attempts not to influence the communication between client and brokers, so
the messages that could not be decoded (due to Kafka client or broker running a newer version than
supported by this filter) are forwarded as-is. However this requires the upstream Kafka cluster to
be configured in proxy-aware fashion (see :ref:`config_network_filters_kafka_broker_config_no_mutation`).

If configured to mutate the received traffic, Envoy broker filter can be used to proxy a Kafka broker
without any changes in the broker configuration.
This requires the broker filter to be provided with rewrite rules so the addresses advertised by
the Kafka brokers can be changed to the Envoy listener addresses
(see :ref:`config_network_filters_kafka_broker_config_with_mutation`).

* This filter should be configured with the type URL ``type.googleapis.com/envoy.extensions.filters.network.kafka_broker.v3.KafkaBroker``.
* :ref:`v3 API reference <envoy_v3_api_msg_extensions.filters.network.kafka_broker.v3.KafkaBroker>`

.. attention::

   The Kafka broker filter is only included in :ref:`contrib images <install_contrib>`

.. attention::

   The kafka_broker filter is experimental and is currently under active development.
   Capabilities will be expanded over time and the configuration structures are likely to change.

.. _config_network_filters_kafka_broker_config_no_mutation:

Configuration (no traffic mutation)
-----------------------------------

The Kafka Broker filter can run without rewriting any requests / responses.

The filter should be chained with the TCP proxy filter as shown in the snippet below:

.. code-block:: yaml

  listeners:
  - address:
      socket_address:
        address: 127.0.0.1 # Host that Kafka clients should connect to (i.e. bootstrap.servers).
        port_value: 19092  # Port that Kafka clients should connect to (i.e. bootstrap.servers).
    filter_chains:
    - filters:
      - name: envoy.filters.network.kafka_broker
        typed_config:
          "@type": type.googleapis.com/envoy.extensions.filters.network.kafka_broker.v3.KafkaBroker
          stat_prefix: exampleprefix
      - name: envoy.filters.network.tcp_proxy
        typed_config:
          "@type": type.googleapis.com/envoy.extensions.filters.network.tcp_proxy.v3.TcpProxy
          stat_prefix: tcp
          cluster: localkafka
  clusters:
  - name: localkafka
    connect_timeout: 0.25s
    type: strict_dns
    lb_policy: round_robin
    load_assignment:
      cluster_name: some_service
      endpoints:
        - lb_endpoints:
          - endpoint:
              address:
                socket_address:
                  address: 127.0.0.1 # Kafka broker's host.
                  port_value: 9092   # Kafka broker's port.

The Kafka broker then needs to advertise the Envoy listener port instead of its own -
this makes the downstream clients make any new connections to Envoy only.

.. code-block:: text

  # Listener value needs to be equal to cluster value in Envoy config
  # (will receive payloads from Envoy).
  listeners=PLAINTEXT://127.0.0.1:9092

  # Advertised listener value needs to be equal to Envoy's listener
  # (will make clients discovering this broker talk to it through Envoy).
  advertised.listeners=PLAINTEXT://127.0.0.1:19092

.. _config_network_filters_kafka_broker_config_with_mutation:

Configuration (with traffic mutation)
-------------------------------------

The Kafka Broker filter can mutate the contents of received responses to enable easier proxying
of Kafka clusters.

The below example shows a configuration for an Envoy instance that attempts to proxy brokers
in 2-node cluster:

.. code-block:: yaml

  listeners:
  - address: # This listener proxies broker 1.
      socket_address:
        address: envoy.example.org # Host that Kafka clients should connect to (i.e. bootstrap.servers).
        port_value: 19092          # Port that Kafka clients should connect to (i.e. bootstrap.servers).
    filter_chains:
    - filters:
      - name: envoy.filters.network.kafka_broker
        typed_config:
          "@type": type.googleapis.com/envoy.extensions.filters.network.kafka_broker.v3.KafkaBroker
          stat_prefix: exampleprefix1
          id_based_broker_address_rewrite_spec: &kafka_rewrite_spec
            rules:
            - id: 1
              host: envoy.example.org
              port: 19092
            - id: 2
              host: envoy.example.org
              port: 19093
      - name: envoy.filters.network.tcp_proxy
        typed_config:
          "@type": type.googleapis.com/envoy.extensions.filters.network.tcp_proxy.v3.TcpProxy
          stat_prefix: tcp
          cluster: broker1cluster
  - address: # This listener proxies broker 2.
      socket_address:
        address: envoy.example.org # Host that Kafka clients should connect to (i.e. bootstrap.servers).
        port_value: 19093          # Port that Kafka clients should connect to (i.e. bootstrap.servers).
    filter_chains:
    - filters:
      - name: envoy.filters.network.kafka_broker
        typed_config:
          "@type": type.googleapis.com/envoy.extensions.filters.network.kafka_broker.v3.KafkaBroker
          stat_prefix: exampleprefix2
          id_based_broker_address_rewrite_spec: *kafka_rewrite_spec
      - name: envoy.filters.network.tcp_proxy
        typed_config:
          "@type": type.googleapis.com/envoy.extensions.filters.network.tcp_proxy.v3.TcpProxy
          stat_prefix: tcp
          cluster: broker2cluster

  clusters:
  - name: broker1cluster
    connect_timeout: 0.25s
    type: strict_dns
    lb_policy: round_robin
    load_assignment:
      cluster_name: some_service
      endpoints:
        - lb_endpoints:
          - endpoint:
              address:
                socket_address:
                  address: broker1.example.org # Kafka broker's host for broker 1.
                  port_value: 9092             # Kafka broker's port for broker 1.
  - name: broker1cluster
    connect_timeout: 0.25s
    type: strict_dns
    lb_policy: round_robin
    load_assignment:
      cluster_name: some_service
      endpoints:
        - lb_endpoints:
          - endpoint:
              address:
                socket_address:
                  address: broker2.example.org # Kafka broker's host for broker 2.
                  port_value: 9092             # Kafka broker's port for broker 2.

The address rewrite rules should cover all brokers present in the cluster - YAML blocks can be
used to avoid repetition.

The responses that can be mutated are:

* metadata (all partition discovery operations),
* find coordinator (used by consumer groups and transactions),
* describe cluster.

.. _config_network_filters_kafka_broker_debugging:

Debugging
---------

Java clients can see the hosts used if they set the log level of
`org.apache.kafka.clients.NetworkClient` to `debug` - only Envoy's listeners should be visible
in the logs.

.. code-block:: text

  [DEBUG] [NetworkClient] Initiating connection to node localhost:19092 (id: -1 rack: null) using address localhost/127.0.0.1
  [DEBUG] [NetworkClient] Completed connection to node -1. Fetching API versions.
  [DEBUG] [NetworkClient] Initiating connection to node localhost:19092 (id: 1 rack: null) using address localhost/127.0.0.1
  [DEBUG] [NetworkClient] Completed connection to node 1. Fetching API versions.
  [DEBUG] [NetworkClient] Initiating connection to node localhost:19094 (id: 3 rack: null) using address localhost/127.0.0.1
  [DEBUG] [NetworkClient] Initiating connection to node localhost:19093 (id: 2 rack: null) using address localhost/127.0.0.1
  [DEBUG] [NetworkClient] Completed connection to node 2. Fetching API versions.
  [DEBUG] [NetworkClient] Completed connection to node 3. Fetching API versions.

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
