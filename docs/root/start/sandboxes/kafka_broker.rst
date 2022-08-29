.. _install_sandboxes_kafka:

Kafka Broker
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


Step 2: Test Envoy's Kafka Broker
*********************************


.. seealso::

