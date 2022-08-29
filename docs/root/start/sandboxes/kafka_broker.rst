.. _install_sandboxes_kafka:

Kafka broker
============

.. sidebar:: Requirements

   .. include:: _include/docker-env-setup-link.rst

   :ref:`curl <start_sandboxes_setup_curl>`
        Used to make ``HTTP`` requests.


Step 1: Start all of our containers
***********************************

Change to the ``examples/kafka-broker`` directory.

.. code-block:: console

    $ pwd
    envoy/examples/kafka-broker
    $ docker-compose pull
    $ docker-compose up --build -d
    $ docker-compose ps


Step 2: Create a Kafka topic
****************************


Step 3: Check the Kafka topic
*****************************


Step 4: Send a message using the Kafka producer
***********************************************


Step 5: Receive a message using the Kafka consumer
**************************************************


Step 6: Check admin kafka_broker stats
**************************************


Step 7: Check admin kafka_service stats
***************************************


.. seealso::

