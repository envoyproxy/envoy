.. _install_sandboxes_kafka:

Kafka Filter
============

In this example, we show how the :ref:`Kafka filter <config_network_filters_kafka_broker>` can be used with the Envoy proxy. The Envoy proxy configuration includes a Kafka filter that undewrstand Kafka network protocol and collects Kafka-specific
metrics.

Running the Sandboxes
~~~~~~~~~~~~~~~~~~~~~

The following documentation runs through the setup of both services.

**Step 1: Install Docker**

Ensure that you have a recent versions of ``docker`` and ``docker-compose``.

A simple way to achieve this is via the `Docker Desktop <https://www.docker.com/products/docker-desktop>`_.

**Step 2: Clone the Envoy repo and start all of our containers**

If you have not cloned the Envoy repo, clone it with ``git clone git@github.com:envoyproxy/envoy``
or ``git clone https://github.com/envoyproxy/envoy.git``


Terminal 1

.. code-block:: console

  $ pwd
  envoy/examples/kafka
  $ docker-compose pull
  $ docker-compose up --build -d
  $ docker-compose ps

         Name                     Command               State                              Ports
  ------------------------------------------------------------------------------------------------------------------------
  kafka_kafka_1       start-kafka.sh                   Up      0.0.0.0:31001->31001/tcp, 9092/tcp
  kafka_proxy_1       /docker-entrypoint.sh /bin ...   Up      10000/tcp, 0.0.0.0:19092->19092/tcp, 0.0.0.0:8001->8001/tcp
  kafka_zookeeper_1   /bin/sh -c /usr/sbin/sshd  ...   Up      2181/tcp, 22/tcp, 2888/tcp, 3888/tcp


**Step 3: Produce events**

Use ``kafka scripts`` to issue some commands and verify they are routed via Envoy. Note
that the current implementation of the protocol filter was tested with Kafka- 2.0. It may, however, not work with other versions of Kafka due to differences
in the protocol implementation.

Terminal 1

.. code-block:: console

  $ docker run --rm -it --network envoymesh wurstmeister/kafka:2.11-2.0.0 /opt/kafka/bin/kafka-console-producer.sh --broker-list envoy:19092 --topic test_topic
  > test_event_1
  > test_event_2

**Step 3: Consume events**

Terminal 2

.. code-block:: console

  $ docker run --rm -it --network envoymesh wurstmeister/kafka:2.11-2.0.0 /opt/kafka/bin/kafka-console-consumer.sh --bootstrap-server envoy:19092 --topic test_topic --from-beginning
  test_event_1
  test_event_2

**Step 4: Check stats**

Check stats were updated.

Terminal 1

.. code-block:: console

  $ curl -s http://localhost:8001/stats?filter=kafka.kafka_custom_prefix.request
  kafka.kafka_custom_prefix.request.api_versions_request: 7
  kafka.kafka_custom_prefix.request.controlled_shutdown_request: 0
  kafka.kafka_custom_prefix.request.create_acls_request: 0
  kafka.kafka_custom_prefix.request.create_delegation_token_request: 0
  kafka.kafka_custom_prefix.request.create_partitions_request: 0
  kafka.kafka_custom_prefix.request.create_topics_request: 0
  kafka.kafka_custom_prefix.request.delete_acls_request: 0
  kafka.kafka_custom_prefix.request.delete_groups_request: 0
  kafka.kafka_custom_prefix.request.delete_records_request: 0
  kafka.kafka_custom_prefix.request.delete_topics_request: 0
  kafka.kafka_custom_prefix.request.describe_acls_request: 0
  kafka.kafka_custom_prefix.request.describe_configs_request: 0
  kafka.kafka_custom_prefix.request.describe_delegation_token_request: 0
  kafka.kafka_custom_prefix.request.describe_groups_request: 0
  kafka.kafka_custom_prefix.request.describe_log_dirs_request: 0
  kafka.kafka_custom_prefix.request.elect_preferred_leaders_request: 0
  kafka.kafka_custom_prefix.request.end_txn_request: 0
  kafka.kafka_custom_prefix.request.expire_delegation_token_request: 0
  kafka.kafka_custom_prefix.request.failure: 0
  kafka.kafka_custom_prefix.request.fetch_request: 394
  kafka.kafka_custom_prefix.request.find_coordinator_request: 6
  kafka.kafka_custom_prefix.request.heartbeat_request: 66
  kafka.kafka_custom_prefix.request.init_producer_id_request: 0
  kafka.kafka_custom_prefix.request.join_group_request: 1
  kafka.kafka_custom_prefix.request.leader_and_isr_request: 2
  kafka.kafka_custom_prefix.request.leave_group_request: 1
  kafka.kafka_custom_prefix.request.list_groups_request: 0
  kafka.kafka_custom_prefix.request.list_offset_request: 1
  kafka.kafka_custom_prefix.request.metadata_request: 11
  kafka.kafka_custom_prefix.request.offset_commit_request: 40

**Step 5: Check TCP stats**

Check TCP stats were updated.

Terminal 1

.. code-block:: console

  $ curl -s http://localhost:8001/stats?filter=kafka_tcp
  tcp.kafka_tcp.downstream_cx_no_route: 0
  tcp.kafka_tcp.downstream_cx_rx_bytes_buffered: 3068
  tcp.kafka_tcp.downstream_cx_rx_bytes_total: 42707
  tcp.kafka_tcp.downstream_cx_total: 8
  tcp.kafka_tcp.downstream_cx_tx_bytes_buffered: 0
  tcp.kafka_tcp.downstream_cx_tx_bytes_total: 16207
  tcp.kafka_tcp.downstream_flow_control_paused_reading_total: 0
  tcp.kafka_tcp.downstream_flow_control_resumed_reading_total: 0
  tcp.kafka_tcp.idle_timeout: 0
  tcp.kafka_tcp.upstream_flush_active: 0
  tcp.kafka_tcp.upstream_flush_total: 0

