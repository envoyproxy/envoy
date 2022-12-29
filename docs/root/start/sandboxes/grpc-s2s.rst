.. _install_sandboxes_grpc_s2s:

gRPC Service to Service
=======================

.. sidebar:: Requirements

   .. include:: _include/docker-env-setup-link.rst

This example demonstrates Envoy's support for routing `gRPC requests <https://www.envoyproxy.io/docs/envoy/latest/api-v3/config/route/v3/route_components.proto#config-route-v3-routematch>`__.

The sandbox consists of two gRPC services, `Hello` and `World`. Each service implements a Unary-Unary
gRPC method, ``Greet()``. We run 2 instances of each service.

Two separate Envoy instances, `envoy-hello` and `envoy-world` are configured as a proxy for both services.

Using a gRPC client, `grpccurl`, we will make a request to the `Hello` service's `Greet()` method via
the `envoy-hello` instance.

.. literalinclude:: _include/grpc-s2s/hello/envoy-proxy.yaml
   :language: yaml
   :lines: 16-34
   :linenos:
   :emphasize-lines: 6-11
   :caption: Envoy configuration with static route mirror policy :download:`front-envoy.yaml <_include/route-mirror/front-envoy.yaml>`

The `Hello` service's `Greet()` method invokes the `Greet()` method of the `World` service
via the `envoy-world` envoy proxy instance.

.. literalinclude:: _include/grpc-s2s/world/envoy-proxy.yaml
   :language: yaml
   :lines: 16-34
   :linenos:
   :emphasize-lines: 6-11
   :caption: Envoy configuration with static route mirror policy :download:`front-envoy.yaml <_include/route-mirror/front-envoy.yaml>`

The request flow looks as follows for this sandbox:

``[client](gRPC) -> [envoy-hello] -> [hello](2 instances) -> [envoy-world] -> [world](2 instances)``

Another feature demonstrated in this example is integrating
`gRPC health checks <https://github.com/grpc/grpc/blob/master/doc/health-checking.md>`__
with Envoy's active health checking for upstream clusters using
`GrpcHealthCheck <https://www.envoyproxy.io/docs/envoy/latest/api-v3/config/core/v3/health_check.proto#envoy-v3-api-msg-config-core-v3-healthcheck-grpchealthcheck>`__.

.. literalinclude:: _include/grpc-s2s/hello/envoy-proxy.yaml
   :language: yaml
   :lines: 16-34
   :linenos:
   :emphasize-lines: 6-11
   :caption: Envoy configuration with static route mirror policy :download:`front-envoy.yaml <_include/route-mirror/front-envoy.yaml>`

The Envoy proxy for the World service has healthchecks configured similarly:

   .. literalinclude:: _include/grpc-s2s/world/envoy-proxy.yaml
   :language: yaml
   :lines: 16-34
   :linenos:
   :emphasize-lines: 6-11
   :caption: Envoy configuration with static route mirror policy :download:`front-envoy.yaml <_include/route-mirror/front-envoy.yaml>`

.. note::

   Mention the stuff about specifying the configuration specifically for the sandbox


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

Let's verify that from the logs:

.. code-block:: console

   $ docker-compose logs hello | grep hello-1
   grpc-s2s-hello-1  | 2022/12/29 21:50:09 starting grpc on :8081
   grpc-s2s-hello-1  | 2022/12/29 21:51:20 Marking service Hello as unhealthy

The ``Hello`` gRPC service instance will now have the status of the service to
be ``NOT_SERVING`` as per the `gRPC Health checking
protocol <https://github.com/grpc/grpc/blob/master/doc/health-checking.md>`__.

Step 6: Mark an instance of World unhealthy
*******************************************

We will now send a ``SIGUSR1`` signal to the first instance of the ``World`` gRPC service
so that it marks itself unhealthy:

.. code-block:: console

   $ docker-compose exec -ti --index 1 world kill -SIGUSR1 1

Let's verify that from the logs:

.. code-block:: console

   $ docker-compose logs world | grep world-1
   grpc-s2s-world-1  | 2022/12/29 21:50:09 starting grpc on :8082
   grpc-s2s-world-1  | 2022/12/29 21:51:20 Marking service World as unhealthy

The ``World`` gRPC service instance will now have the status of the service to
be ``NOT_SERVING`` as per the `gRPC Health checking
protocol <https://github.com/grpc/grpc/blob/master/doc/health-checking.md>`__.

Step 7: Query number of healthy instances of Hello service
**********************************************************

Let's query the stats from ``envoy-hello`` service to check the number of healthy
instances for ``hello``:

.. code-block:: console

   $ curl -s http://localhost:12800/stats | grep "cluster.hello.health_check.healthy"
   cluster.hello.health_check.healthy: 1

The number of healthy instances for the ``Hello`` gRPC service is now 1, as
Envoy has detected one instance of the service as unhealthy.

Step 8: Query number of healthy instances of World service
**********************************************************

Let's query the stats from ``envoy-world`` service to check the number of healthy
instances for ``world``:

.. code-block:: console

   $ curl -s http://localhost:12801/stats | grep "cluster.world.health_check.healthy"
   cluster.world.health_check.healthy: 1

The number of healthy instances for the ``World`` gRPC service is now 1, as
Envoy has detected one instance of the service as unhealthy.

Step 9: Make an example request to Hello
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

   :ref:`Envoy request mirror policy <envoy_v3_api_msg_config.route.v3.RouteAction.RequestMirrorPolicy>`
    Learn more Envoy's request mirroring policy.
