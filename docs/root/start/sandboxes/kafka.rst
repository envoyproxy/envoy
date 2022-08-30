.. _install_sandboxes_kafka:

Kafka broker
============

.. sidebar:: Requirements

   .. include:: _include/docker-env-setup-link.rst

   :ref:`curl <start_sandboxes_setup_curl>`
        Used to make ``HTTP`` requests.


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
  $ docker-compose run --rm kafka-client --bootstrap-server proxy:10000 --create --topic $TOPIC


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

You can check the broker stats by querying the Envoy admin interface:

.. code-block:: console

  $ curl -s "http://localhost:8001/stats?filter=kafka.kafka_broker" | grep -v ": 0"
  kafka.kafka_broker.request.create_topics_request: 1
  kafka.kafka_broker.request.api_versions_request: 4
  kafka.kafka_broker.request.find_coordinator_request: 1
  kafka.kafka_broker.request.metadata_request: 4
  kafka.kafka_broker.response.api_versions_response: 4
  kafka.kafka_broker.response.find_coordinator_response: 1
  kafka.kafka_broker.response.metadata_response: 4


Step 7: Check admin ``kafka_service`` stats
*******************************************

Envoy also records stats fot the Kafka service:

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
