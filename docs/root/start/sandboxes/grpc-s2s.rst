.. _install_sandboxes_grpc_s2s:

gRPC Service to Service
=======================

.. sidebar:: Requirements

   .. include:: _include/docker-env-setup-link.rst

This example demonstrates Envoy's support for routing `gRPC requests <https://www.envoyproxy.io/docs/envoy/latest/api-v3/config/route/v3/route_components.proto#config-route-v3-routematch>`__.

The sandbox consists of two gRPC services, `Hello` and `World`. We run 2 instances of each service.

Two separate Envoy instances, `envoy-hello` and `envoy-world` are configured as a proxy for both services.

A gRPC client, such as `grpccurl` makes a request to the `Hello` service's `Greet()` method via
the `envoy-hello` instance.

.. literalinclude:: _include/grpc-s2s/hello/envoy-proxy.yaml
   :language: yaml
   :lines: 16-34
   :linenos:
   :emphasize-lines: 6-11
   :caption: Envoy configuration with static route mirror policy :download:`front-envoy.yaml <_include/route-mirror/front-envoy.yaml>`


The `Hello` service's `Greet()` method then invokes the `Greet()` method of the `World` service
via the `envoy-world` envoy proxy instance.

.. literalinclude:: _include/grpc-s2s/world/envoy-proxy.yaml
   :language: yaml
   :lines: 16-34
   :linenos:
   :emphasize-lines: 6-11
   :caption: Envoy configuration with static route mirror policy :download:`front-envoy.yaml <_include/route-mirror/front-envoy.yaml>`


The request flow from a client application looks as follows for this sandbox:

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

The Envoy proxy for the World service has healthchecks configured similarly.

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

Step 3: Query number of healthy instances of Hello and World services
*********************************************************************

Let's query the stats from ``envoy-hello`` service to check the number of healthy
instances for ``Hello``:

.. code-block:: console

  $ docker run --network=host fullstorydev/grpcurl -plaintext localhost:12000 Hello/Greet
  {
    "reply": "hello world"
  }

  TBD

.. seealso::

   :ref:`Envoy request mirror policy <envoy_v3_api_msg_config.route.v3.RouteAction.RequestMirrorPolicy>`
    Learn more Envoy's request mirroring policy.
