.. _install_sandboxes_locality_load_balancing:

Locality Weighted Load Balancing
================================

.. sidebar:: Requirements

   .. include:: _include/docker-env-setup-link.rst

   :ref:`curl <start_sandboxes_setup_curl>`
        Used to make ``HTTP`` requests.

This example demonstrates the :ref:`locality weighted load balancing <arch_overview_load_balancing_locality_weighted_lb>` feature in Envoy proxy. The demo simulates a scenario that a backend service resides in two local zones and one remote zone.

The components used in this demo are as follows:

- A client container: runs Envoy proxy
- Backend container in the same locality as the client, with priority set to 0, referred to as ``local-1``.
- Backend container in the same locality as the client, with priority set to 1, referred to as ``local-2``.
- Backend container in the the remote locality, with priority set to 1, referred to as ``remote-1``.
- Backend container in the the remote locality, with priority set to 2, referred to as ``remote-2``.

The client Envoy proxy configures the 4 backend containers in the same Envoy cluster, so that Envoy handles load balancing to those backend servers. From here we can see, we have localities with 3 different priorities:

- priority 0: ``local-1``
- priority 1: ``local-2`` and ``remote-1``
- priority 2: ``remote-2``

In Envoy, when the healthiness of a given locality drops below a threshold (71% by default), the next priority locality will start to share the request loads. The demo below will show this behavior.

Step 1: Start all of our containers
***********************************

In terminal, move to the ``examples/locality_load_balancing`` directory.

To build this sandbox example and start the example services, run the following commands:

.. code-block:: console

    # Start demo
    $ docker-compose up --build -d

The locality configuration is set in the client container via static Envoy configuration file. Please refer to the ``cluster`` section of the :download:`proxy configuration <_include/locality-load-balancing/envoy-proxy.yaml>` file.

Step 2: Scenario with one replica in the highest priority locality
******************************************************************

In this scenario, each locality has 1 healthy replica running and all the requests should be sent to the locality with the highest priority (i.e. lowest integer set for priority - ``0``), which is ``local-1``.

.. code-block:: console

    # all requests to local-1
    $ docker-compose exec -T client-envoy python3 client.py http://localhost:3000/ 100
    Hello from backend-local-1!: 100, 100.0%
    Failed: 0

If locality ``local-1`` becomes unhealthy (i.e. fails the Envoy health check), the requests should be load balanced among the subsequent priority localities, which are ``local-2`` and ``remote-1``. They both have priority 1. We then send 100 requests to the backend cluster, and check the responders.

.. code-block:: console

    # bring down local-1
    $ docker-compose exec -T client-envoy curl -s locality-load-balancing_backend-local-1_1:8000/unhealthy
    [backend-local-1] Set to unhealthy

    # local-2 and remote-1 localities split the traffic 50:50
    $ docker-compose exec -T client-envoy python3 client.py http://localhost:3000/ 100
    Hello from backend-remote-1!: 51, 51.0%
    Hello from backend-local-2!: 49, 49.0%
    Failed: 0

Now if ``local-2`` becomes unhealthy also, priority 1 locality is only 50% healthy. Thus priority 2 locality starts to share the request load. Requests will be sent to both ``remote-1`` and ``remote-2``.

.. code-block:: console

    # bring down local-2
    $ docker-compose exec -T client-envoy curl -s locality-load-balancing_backend-local-2_1:8000/unhealthy

    # remote-1 locality receive 100% of the traffic
    $ docker-compose exec -T client-envoy python3 client.py http://localhost:3000/ 100
    Hello from backend-remote-1!: actual weight 69.0%
    Hello from backend-remote-2!: actual weight 31.0%
    Failed: 0


Step 3: Recover servers
***********************

Before moving on, we need to server local-1 and local-2 first.

.. code-block:: console

    # recover local-1 and local-2 after the demo
    $ docker-compose exec -T client-envoy curl -s locality-load-balancing_backend-local-1_1:8000/healthy
    $ docker-compose exec -T client-envoy curl -s locality-load-balancing_backend-local-2_1:8000/healthy


Step 4: Scenario with multiple replicas in the highest priority locality
************************************************************************

To demonstrate how locality based load balancing works in multiple replicas setup, let's now scale up the ``local-1`` locality to 5 replicas.

.. code-block:: console

    $ docker-compose up --scale backend-local-1=5 -d

We are going to show the scenario that ``local-1`` is just partially healthy. So let's bring down 4 of the replicas in ``local-1``.

.. code-block:: console

    # bring down local-1 replicas
    $ docker-compose exec -T client-envoy curl -s locality-load-balancing_backend-local-1_2:8000/unhealthy
    $ docker-compose exec -T client-envoy curl -s locality-load-balancing_backend-local-1_3:8000/unhealthy
    $ docker-compose exec -T client-envoy curl -s locality-load-balancing_backend-local-1_4:8000/unhealthy
    $ docker-compose exec -T client-envoy curl -s locality-load-balancing_backend-local-1_5:8000/unhealthy

Then we check the endpoints again:

.. code-block:: console

    # check healthiness
    $ docker-compose exec -T client-envoy curl -s localhost:8001/clusters | grep health_flags

    backend::172.28.0.4:8000::health_flags::/failed_active_hc
    backend::172.28.0.2:8000::health_flags::/failed_active_hc
    backend::172.28.0.5:8000::health_flags::/failed_active_hc
    backend::172.28.0.6:8000::health_flags::/failed_active_hc
    backend::172.28.0.7:8000::health_flags::healthy
    backend::172.28.0.8:8000::health_flags::healthy
    backend::172.28.0.3:8000::health_flags::healthy

We can confirm that 4 backend endpoints become unhealthy.

Now we send the 100 requests again.

.. code-block:: console

    # watch traffic change
    $ docker-compose exec -T client-envoy python3 client.py http://localhost:3000/ 100

    Hello from backend-remote-1!: actual weight 37.0%
    Hello from backend-local-2!: actual weight 36.0%
    Hello from backend-local-1!: actual weight 27.0%
    Failed: 0

As ``local-1`` does not have enough healthy workloads, requests are partially shared by secondary localities.

If we bring down all the servers in priority 1 locality, it will make priority 1 locality 0% healthy. The traffic should split between priority 0 and priority 2 localities.

.. code-block:: console

    $ docker-compose exec -T client-envoy curl -s locality-load-balancing_backend-local-2_1:8000/unhealthy
    $ docker-compose exec -T client-envoy curl -s locality-load-balancing_backend-remote-1_1:8000/unhealthy
    $ docker-compose exec -T client-envoy python3 client.py http://localhost:3000/ 100

    Hello from backend-remote-2!: actual weight 77.0%
    Hello from backend-local-1!: actual weight 23.0%
    Failed: 0
