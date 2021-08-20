.. _install_sandboxes_locality_load_balancing:

Locality Weighted Load Balancing
================================

.. sidebar:: Requirements

   .. include:: _include/docker-env-setup-link.rst

   :ref:`curl <start_sandboxes_setup_curl>`
        Used to make ``HTTP`` requests.

This example creates a setup for demonstrating the :ref: `locality weighted load balancing <_arch_overview_load_balancing_locality_weighted_lb>` feature in Envoy proxy. The demo simulates a scenario that a backend service resides in two local zones and one remote zone.

The components used in this demo are as follows:

- A client container: runs Envoy proxy
- Backend container in the same locality as the client, namely ``local-a``.
- Backend container in a different locality, namely ``local-b``.
- Backend container in yet another locality, namely ``remote``.

The client Envoy proxy configures the 3 backend containers in the same Envoy cluster, so that Envoy handle load balancing to those backend servers.

Step 1: Start all of our containers
***********************************

Change to the ``examples/locality_load_balancing`` directory.

To build this sandbox example and start the example services, run the following commands:

.. code:: shell

    # Start demo
    $ docker-compose up --build -d

The locality configuration is set in the client container via static Envoy configuration file. Please refer to the `clusters configuration <_include/locality-load-balancing/configs/cds.yaml>_` file.

Step 2: Scenario with 1 replica per locaility
*********************************************

In this scenario, each locality has 1 healthy replica running. In this case, all the requests should be sent to the locality with the highest priority, which is ``local-a``.

.. code:: shell

    # all requests to local-1
    $ docker-compose exec -T client-envoy python3 client.py http://localhost:3000/ 100
    Hello from backend-local-1!
    : 100, 100.0%
    Failed: 0

If locality ``local-1`` becomes unhealthy, the requests should be load balanced among the subsequent priority localities, which are ``local-b`` and ``remote``. We are sending 100 requests to the backend cluster, and observe the receivers.

.. code:: shell

    # bring down local-1
    $ docker-compose exec -T client-envoy curl -s "locality-load-balancing_backend-local-1_1":8000/unhealthy
    [backend-local-1] Set to unhealthy

    # local-2 and remote locaility split the traffic 50:50
    $ docker-compose exec -T client-envoy python3 client.py http://localhost:3000/ 100
    Hello from backend-remote!
    : 51, 51.0%
    Hello from backend-local-2!
    : 49, 49.0%
    Failed: 0

Now if ``local-2`` becomes unhealthy also, the requests will be solely sent to the ``remote`` locality.

.. code:: shell

    # bring down local-2
    $ docker-compose exec -T client-envoy curl -s "locality-load-balancing_backend-local-2_1":8000/unhealthy

    # remote locaility receive 100% of the traffic
    $ docker-compose exec -T client-envoy python3 client.py http://localhost:3000/ 100
    Hello from backend-remote!
    : 100, 100.0%
    Failed: 0

    # recover local-1 and local-2 after the demo
    $ docker-compose exec -T client-envoy curl -s locality-load-balancing_backend-local-1_1:8000/healthy
    $ docker-compose exec -T client-envoy curl -s locality-load-balancing_backend-local-2_1:8000/healthy


Step 3: Scenario with multiple replicas per locaility
*****************************************************

Continue from previous step. We first scale up the ``local-1`` locality to 5 replicas.

.. code:: shell

    $ docker-compose up --scale backend-local-1=5 -d

We are going to show the scenario that ``local-1`` is just partially healthy. So let's bring down 4 of the replicas in ``local-1``.

.. code:: shell

    # bring down local-1 replicas
    $ docker-compose exec -T client-envoy curl -s locality-load-balancing_backend-local-1_2:8000/unhealthy
    $ docker-compose exec -T client-envoy curl -s locality-load-balancing_backend-local-1_3:8000/unhealthy
    $ docker-compose exec -T client-envoy curl -s locality-load-balancing_backend-local-1_4:8000/unhealthy
    $ docker-compose exec -T client-envoy curl -s locality-load-balancing_backend-local-1_5:8000/unhealthy

    # check healthiness
    $ docker-compose exec -T client-envoy curl -s localhost:8001/clusters | grep health_flags

    backend::172.28.0.4:8000::health_flags::/failed_active_hc
    backend::172.28.0.2:8000::health_flags::/failed_active_hc
    backend::172.28.0.5:8000::health_flags::/failed_active_hc
    backend::172.28.0.6:8000::health_flags::/failed_active_hc
    backend::172.28.0.7:8000::health_flags::healthy
    backend::172.28.0.8:8000::health_flags::healthy
    backend::172.28.0.3:8000::health_flags::healthy

You should see that 4 backend endpoints become not healthy.

Now we send the 100 requests again.

.. code:: shell

    # watch traffic change
    $ docker-compose exec -T client-envoy python3 client.py http://localhost:3000/ 100

    Hello from backend-local-2!
    : 35, 35.0%
    Hello from backend-remote!
    : 33, 33.0%
    Hello from backend-local-1!
    : 32, 32.0%
    Failed: 0

As ``local-1`` does not have enough healthy workloads, requests are partially shared by secondary localities.

**Conclusion:** default overprovisioning factor is 1.4, which means,
when the highest cluster has less than 71% of the workloads health, the
LB will gradually shift traffic to other localities.
