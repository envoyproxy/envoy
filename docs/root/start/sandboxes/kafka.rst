.. _install_sandboxes_kafka:

Kafka broker
============

.. sidebar:: Requirements

   .. include:: _include/docker-env-setup-link.rst

   :ref:`curl <start_sandboxes_setup_curl>`
        Used to make ``HTTP`` requests.

This example demonstrates some basic operations with a Kafka broker proxied through Envoy.

For your convenience, the :download:`composition <_include/kafka/docker-compose.yaml>` provides
a dockerized Kafka client.

If you have the ``kafka-console-*`` binaries installed on your host system, you can instead follow
the examples using the host binary with ``--bootstrap-server localhost:10000``.

Statistics collected by Envoy for the Kafka broker extension and related cluster metrics are also demonstrated.


Step 1: Start all of our containers
***********************************

Change to the ``examples/kafka`` directory.

.. code-block:: console

  $ pwd
  envoy/examples/kafka
  $ docker-compose pull
  $ docker-compose up --build -d
  $ docker-compose ps

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
  $ docker-compose run --rm kafka-client kafka-topics --bootstrap-server proxy:10000 --create --topic $TOPIC


Step 3: Check the Kafka topic
*****************************

You can view the topics that Kafka is aware of with the ``kafka-topics --list`` argument.

Check that the topic you created exists:

.. code-block:: console

  $ docker-compose run --rm kafka-client kafka-topics --bootstrap-server proxy:10000 --list | grep $TOPIC


Step 4: Send a message using the Kafka producer
***********************************************

Next, send a message for the topic you have created using the ``kafka-console-producer``:

.. code-block:: console

  $ export MESSAGE="Welcome to Envoy and Kafka broker filter!"
  $ docker-compose run --rm kafka-client /bin/bash -c " \
      echo $MESSAGE \
      | kafka-console-producer --request-required-acks 1 --broker-list proxy:10000 --topic $TOPIC"


Step 5: Receive a message using the Kafka consumer
**************************************************

Now you can receive the message using the ``kafka-console-consumer`` :

.. code-block:: console

  $ docker-compose run --rm kafka-client kafka-console-consumer --bootstrap-server proxy:10000 --topic $TOPIC --from-beginning --max-messages 1 | grep "$MESSAGE"


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

.. seealso::

  :ref:`Envoy Kafka broker filter <config_network_filters_kafka_broker>`
    Learn more about the Kafka broker filter.

  `Kafka <https://kafka.apache.org/>`_
    The Apache Kafka.
