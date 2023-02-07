.. _install_sandboxes_grpc_s2s:

gRPC Service to Service
=======================

.. sidebar:: Requirements

   .. include:: _include/docker-env-setup-link.rst

This example demonstrates Envoy's support for routing
`gRPC traffic <https://www.envoyproxy.io/docs/envoy/latest/api-v3/config/route/v3/route_components.proto#config-route-v3-routematch>`__
and integrating `gRPC health checks <https://github.com/grpc/grpc/blob/master/doc/health-checking.md>`__
with Envoy's active health checking for upstream clusters using
`GrpcHealthCheck <https://www.envoyproxy.io/docs/envoy/latest/api-v3/config/core/v3/health_check.proto#envoy-v3-api-msg-config-core-v3-healthcheck-grpchealthcheck>`__.

The sandbox consists of two gRPC services, ``Hello`` and ``World``. Each service implements a Unary-Unary
gRPC method, ``Greet()`` as described in :download:`hello.proto <_include/grpc-s2s/protos/hello.proto>`
and :download:`world.proto <_include/grpc-s2s/protos/world.proto>`.

We run 2 instances of each service using docker-compose's support for running a service
in `replicated mode <https://docs.docker.com/compose/compose-file/deploy/#replicas>`__.

Two separate Envoy instances, `envoy-hello` and `envoy-world` are configured as a proxy for both services.

The request flow looks as follows for this sandbox:

``[client](gRPC) -> [envoy-hello] -> [hello](2 instances) -> [envoy-world] -> [world](2 instances)``

Using a gRPC client, `grpccurl`, we will make a request to the `Hello` service's `Greet()` method via
the `envoy-hello` instance.

We define a route to match any gRPC request and forward it to the ``hello`` cluster:

.. literalinclude:: _include/grpc-s2s/hello/envoy-proxy.yaml
   :language: yaml
   :lines: 9-25
   :linenos:
   :emphasize-lines: 12-16
   :caption: Forward all gRPC requests to the ``hello`` cluster :download:`envoy-proxy.yaml <_include/grpc-s2s/hello/envoy-proxy.yaml>`

The cluster definition for ``hello`` must specify the ``http2_protocol_options`` to
be able to accept gRPC traffic:

.. literalinclude:: _include/grpc-s2s/hello/envoy-proxy.yaml
   :language: yaml
   :lines: 30-38
   :linenos:
   :emphasize-lines: 1-3
   :caption: Accept gRPC traffic in the ``hello`` cluster :download:`envoy-proxy.yaml <_include/grpc-s2s/hello/envoy-proxy.yaml>`

We configure the gRPC health check by configuring the ``health_checks`` object in the cluster
definition:

.. literalinclude:: _include/grpc-s2s/hello/envoy-proxy.yaml
   :language: yaml
   :lines: 30-46
   :linenos:
   :emphasize-lines: 9-16
   :caption: Cluster health check configuration configured to check the health of ``Hello`` gRPC service :download:`envoy-proxy.yaml <_include/grpc-s2s/hello/envoy-proxy.yaml>`

``timeout`` specifies the maximum time to wait to recieve a health check response.

``interval`` specifies the interval between health checks.

``no_traffic_inteval`` specifies the interval between health checks when no traffic has been forwarded
to the cluster. We set this explicitly to a very low value so that the health checks
are carried out at more frequent intervals when there is no traffic, as in the case of this
sandbox and helps with the demo.

``unhealthy_threshold`` specifies the number of unhealthy health checks before a host is marked
unhealthy.

``unhealthy_threshold`` specifies the number of healthy health checks before a host is marked
healthy.

``grpc_health_check`` specifies that ``service_name`` as we are interested in the health
of the ``Hello`` gRPC service and **not** the gRPC server.

.. note::
   A cluster can also specify :ref:`panic_threshold <arch_overview_load_balancing_panic_threshold>`.

   It allows configuration of Envoy behavior when the number of unhealthy hosts
   go beyond a certain threshold.

   For this sandbox, the default threshold of 50% doesn't affect the demo. We will run 2 replicas
   and we will only have one unhealthy host.

   Thus, envoy will not forward traffic to the unhealthy host which is what we want.

The ``envoy-world`` envoy instance is configured similarly :download:`envoy-proxy.yaml <_include/grpc-s2s/world/envoy-proxy.yaml>`.

Step 1: Build the sandbox
*************************

Change to the ``examples/grpc-s2s`` directory.

.. code-block:: console

   $ pwd
   envoy/examples/grpc-s2s
   $ docker-compose build
   $ docker-compose up -d
   $ docker-compose ps
   NAME                     IMAGE                  COMMAND                  SERVICE             CREATED             STATUS                   PORTS
   grpc-s2s-envoy-hello-1   grpc-s2s-envoy-hello   "/docker-entrypoint.…"   envoy-hello         11 seconds ago      Up 5 seconds             10000/tcp, 0.0.0.0:12000->8080/tcp, :::12000->8080/tcp, 0.0.0.0:12800->9090/tcp, :::12800->9090/tcp
   grpc-s2s-envoy-world-1   grpc-s2s-envoy-world   "/docker-entrypoint.…"   envoy-world         11 seconds ago      Up 6 seconds             10000/tcp, 0.0.0.0:12801->9090/tcp, :::12801->9090/tcp
   grpc-s2s-hello-1         grpc-s2s-hello         "/bin/server -world-…"   hello               11 seconds ago      Up 9 seconds (healthy)
   grpc-s2s-hello-2         grpc-s2s-hello         "/bin/server -world-…"   hello               11 seconds ago      Up 8 seconds (healthy)
   grpc-s2s-world-1         grpc-s2s-world         "/bin/server"            world               11 seconds ago      Up 9 seconds (healthy)
   grpc-s2s-world-2         grpc-s2s-world         "/bin/server"            world               11 seconds ago      Up 8 seconds (healthy)


Step 2: Make an example request to Hello
****************************************

Let's send a request to the ``envoy-hello`` service which forwards the request to
``hello``:

.. code-block:: console

   $ docker run --network=host fullstorydev/grpcurl -plaintext localhost:12000 Hello/Greet
   {
     "reply": "hello world"
   }

Step 3: Query number of healthy instances of Hello service
**********************************************************

Let's query the stats from ``envoy-hello`` service to check the number of healthy
instances for ``hello``:

.. code-block:: console

   $ curl -s http://localhost:12800/stats | grep "cluster.hello.health_check.healthy"
   cluster.hello.health_check.healthy: 2

The number of healthy instances for the ``Hello`` gRPC service is 2, as expected.

Step 4: Query number of healthy instances of World service
**********************************************************

Let's query the stats from ``envoy-world`` service to check the number of healthy
instances for ``world``:

.. code-block:: console

   $ curl -s http://localhost:12801/stats | grep "cluster.world.health_check.healthy"
   cluster.world.health_check.healthy: 2

The number of healthy instances for the ``World`` gRPC service is 2, as expected.

Step 5: Mark an instance of Hello unhealthy
*******************************************

We will now send a ``SIGUSR1`` signal to the first instance of the ``Hello`` gRPC service
so that it marks itself unhealthy:

.. code-block:: console

   $ docker-compose exec -ti --index 1 hello kill -SIGUSR1 1

The ``Hello`` gRPC service instance will now have the status of the service to
be ``NOT_SERVING`` as per the `gRPC Health checking
protocol <https://github.com/grpc/grpc/blob/master/doc/health-checking.md>`__.

Let's verify that we now only have one healthy instance of ``Hello``
from the stats of ``envoy-hello``:

.. code-block:: console

   $ curl -s "http://localhost:12800/stats" | grep "cluster.hello.health_check.healthy"
   cluster.hello.health_check.healthy: 1

The number of healthy instances for the ``Hello`` gRPC service is now 1, as
Envoy has detected one instance of the service as unhealthy.

Step 6: Mark an instance of World unhealthy
*******************************************

We will now send a ``SIGUSR1`` signal to the first instance of the ``World`` gRPC service
so that it marks itself unhealthy:

.. code-block:: console

   $ docker-compose exec -ti --index 1 world kill -SIGUSR1 1

The ``World`` gRPC service instance will now have the status of the service to
be ``NOT_SERVING`` as per the `gRPC Health checking
protocol <https://github.com/grpc/grpc/blob/master/doc/health-checking.md>`__.

Let's verify that we now only have one healthy instance of ``World``
from the stats of ``envoy-world``:

.. code-block:: console

   $ curl -s "http://localhost:12801/stats" | grep "cluster.world.health_check.healthy"
   cluster.world.health_check.healthy: 1

The number of healthy instances for the ``World`` gRPC service is now 1, as
Envoy has detected one instance of the service as unhealthy.

Step 7: Make an example request to Hello
****************************************

Let's send a request to the ``envoy-hello`` service which forwards the request to
``hello``:

.. code-block:: console

   $ docker run --network=host fullstorydev/grpcurl -plaintext localhost:12000 Hello/Greet
   {
     "reply": "hello world"
   }

You will see that the request was processed by the second instance of the ``hello`` and ``world``
services:

.. code-block:: console

   $ docker-compose logs hello
   grpc-s2s-hello-2  | 2022/12/29 22:08:34 Hello: Received request

   $ docker-compose logs world
   grpc-s2s-world-2  | 2022/12/29 22:08:34 World: Received request

.. seealso::

   :ref:`Health checking <arch_overview_health_checking>`
    Learn more Envoy's health checking.
