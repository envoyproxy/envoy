.. _install_sandboxes_kafka:

Kafka broker
============

.. sidebar:: Requirements

   .. include:: _include/docker-env-setup-link.rst

   :ref:`curl <start_sandboxes_setup_curl>`
        Used to make HTTP requests.

This example demonstrates some basic operations with a Kafka broker proxied through Envoy.

Statistics collected by Envoy for the Kafka broker extension and related cluster metrics are also demonstrated.

.. note::

   For your convenience, the :download:`composition <_include/kafka/docker-compose.yaml>` provides
   a dockerized Kafka client.

   If you have the ``kafka-console-*`` binaries installed on your host system, you can instead follow
   the examples using the host binary with ``--bootstrap-server localhost:10000``.


Step 1: Start all of our containers
***********************************

Change to the ``kafka`` directory.

.. code-block:: console

  $ pwd
  examples/kafka
  $ docker compose pull
  $ docker compose up --build -d
  $ docker compose ps

           Name                      Command                State                            Ports
  -----------------------------------------------------------------------------------------------------------------------
  kafka_kafka-server_1   /etc/confluent/docker/run      Up             9092/tcp
  kafka_proxy_1          /docker-entrypoint.sh /usr ... Up             0.0.0.0:10000->10000/tcp, 0.0.0.0:8001->8001/tcp
  kafka_zookeeper_1      /etc/confluent/docker/run      Up (healthy)   2181/tcp, 2888/tcp, 3888/tcp


Step 2: Create a Kafka topic
****************************

Start by creating a Kafka topic with the name ``envoy-kafka-broker``:

.. code-block:: console

  $ export TOPIC="envoy-kafka-broker"
  $ docker compose run --rm kafka-client kafka-topics --bootstrap-server proxy:10000 --create --topic $TOPIC


Step 3: Check the Kafka topic
*****************************

You can view the topics that Kafka is aware of with the ``kafka-topics --list`` argument.

Check that the topic you created exists:

.. code-block:: console

  $ docker compose run --rm kafka-client kafka-topics --bootstrap-server proxy:10000 --list | grep $TOPIC


Step 4: Send a message using the Kafka producer
***********************************************

Next, send a message for the topic you have created using the ``kafka-console-producer``:

.. code-block:: console

  $ export MESSAGE="Welcome to Envoy and Kafka broker filter!"
  $ docker compose run --rm kafka-client /bin/bash -c " \
      echo $MESSAGE \
      | kafka-console-producer --request-required-acks 1 --broker-list proxy:10000 --topic $TOPIC"


Step 5: Receive a message using the Kafka consumer
**************************************************

Now you can receive the message using the ``kafka-console-consumer`` :

.. code-block:: console

  $ docker compose run --rm kafka-client kafka-console-consumer --bootstrap-server proxy:10000 --topic $TOPIC --from-beginning --max-messages 1 | grep "$MESSAGE"


Step 6: Check admin ``kafka_broker`` stats
******************************************

When you proxy to the Kafka broker, Envoy records various stats.

You can check the broker stats by querying the Envoy admin interface
(the numbers might differ a little as the kafka-client does not expose precise control over its network traffic):

.. code-block:: console

  $ curl -s "http://localhost:8001/stats?filter=kafka.kafka_broker" | grep -v ": 0" | grep "_request:"
  kafka.kafka_broker.request.api_versions_request: 9
  kafka.kafka_broker.request.create_topics_request: 1
  kafka.kafka_broker.request.fetch_request: 2
  kafka.kafka_broker.request.find_coordinator_request: 8
  kafka.kafka_broker.request.join_group_request: 2
  kafka.kafka_broker.request.leave_group_request: 1
  kafka.kafka_broker.request.list_offsets_request: 1
  kafka.kafka_broker.request.metadata_request: 12
  kafka.kafka_broker.request.offset_fetch_request: 1
  kafka.kafka_broker.request.produce_request: 1
  kafka.kafka_broker.request.sync_group_request: 1


Step 7: Check admin ``kafka_service`` cluster stats
***************************************************

Envoy also records cluster stats for the Kafka service:

.. code-block:: console

  $ curl -s "http://localhost:8001/stats?filter=cluster.kafka_service" | grep -v ": 0"
  cluster.kafka_service.max_host_weight: 1
  cluster.kafka_service.membership_healthy: 1
  cluster.kafka_service.membership_total: 1


Step 8: Test consumer groups
****************************

Consumer groups allow multiple consumers to coordinate consumption of a topic.
When consumers join a group, they use the Kafka group coordination protocol
which Envoy proxies transparently.

Start a consumer in a group. It will wait for messages and then exit after
a timeout:

.. code-block:: console

   $ docker compose run --rm kafka-client \
       kafka-console-consumer --bootstrap-server proxy:10000 \
           --topic $TOPIC --group test-group --timeout-ms 5000

The consumer group protocol generates additional request types. Check that
the group coordination metrics have incremented:

.. code-block:: console

   $ curl -s "http://localhost:8001/stats?filter=kafka.kafka_broker" | grep -E "(find_coordinator|join_group|sync_group|leave_group)" | grep -v ": 0"
   kafka.kafka_broker.request.find_coordinator_request: 1
   kafka.kafka_broker.request.join_group_request: 1
   kafka.kafka_broker.request.leave_group_request: 1
   kafka.kafka_broker.response.find_coordinator_response: 1
   kafka.kafka_broker.response.join_group_response: 1
   kafka.kafka_broker.response.leave_group_response: 1


Step 9: Test additional admin operations
****************************************

The Kafka admin client supports various topic management operations. Test
altering topic configuration:

.. code-block:: console

   $ docker compose run --rm kafka-client \
       kafka-configs --bootstrap-server proxy:10000 \
           --alter --entity-type topics --entity-name $TOPIC \
           --add-config retention.ms=86400000

Add partitions to the topic:

.. code-block:: console

   $ docker compose run --rm kafka-client \
       kafka-topics --bootstrap-server proxy:10000 \
           --alter --topic $TOPIC --partitions 3

Check the admin operation metrics:

.. code-block:: console

   $ curl -s "http://localhost:8001/stats?filter=kafka.kafka_broker" | grep -E "(incremental_alter_configs|create_partitions)" | grep -v ": 0"
   kafka.kafka_broker.request.incremental_alter_configs_request: 1
   kafka.kafka_broker.request.create_partitions_request: 1


Step 10: Test consumer behavior with empty topic
************************************************

Kafka consumers can "long-poll" for messages - they connect and wait for data
even when none is available yet. This is normal Kafka behavior, and Envoy
correctly proxies these fetch requests even when they return no data.

Create a new empty topic to test this:

.. code-block:: console

   $ export EMPTY_TOPIC="empty-topic-test"
   $ docker compose run --rm kafka-client kafka-topics --bootstrap-server proxy:10000 --create --topic $EMPTY_TOPIC

Check the current fetch request count before consuming:

.. code-block:: console

   $ curl -s "http://localhost:8001/stats?filter=kafka.kafka_broker.request.fetch_request" | grep "fetch_request:"
   kafka.kafka_broker.request.fetch_request: 2

Now try to consume from the empty topic. The consumer will poll the broker
multiple times waiting for messages, then timeout after 5 seconds:

.. code-block:: console

   $ docker compose run --rm kafka-client kafka-console-consumer --bootstrap-server proxy:10000 --topic $EMPTY_TOPIC --timeout-ms 5000

Even though no messages were received, Envoy proxied the fetch requests.
Check that the fetch metric increased:

.. code-block:: console

   $ curl -s "http://localhost:8001/stats?filter=kafka.kafka_broker.request.fetch_request" | grep "fetch_request:"
   kafka.kafka_broker.request.fetch_request: 12

The increased count proves that Envoy correctly handles long-polling fetch
requests, even when the topic is empty. This is important for applications
that need to wait for data to arrive.

Clean up the empty topic:

.. code-block:: console

   $ docker compose run --rm kafka-client kafka-topics --bootstrap-server proxy:10000 --delete --topic $EMPTY_TOPIC


Step 11: Delete the main topic
******************************

Clean up by deleting the test topic:

.. code-block:: console

   $ docker compose run --rm kafka-client \
       kafka-topics --bootstrap-server proxy:10000 --delete --topic $TOPIC

Verify the delete operation was tracked:

.. code-block:: console

   $ curl -s "http://localhost:8001/stats?filter=kafka.kafka_broker.request.delete_topics" | grep -v ": 0"
   kafka.kafka_broker.request.delete_topics_request: 1

.. seealso::

  :ref:`Envoy Kafka broker filter <config_network_filters_kafka_broker>`
    Learn more about the Kafka broker filter.

  `Kafka <https://kafka.apache.org/>`_
    The Apache Kafka.
