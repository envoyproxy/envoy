.. _install_sandboxes_route_mirror:

Route mirroring policies
========================

.. sidebar:: Requirements

   .. include:: _include/docker-env-setup-link.rst

This simple example demonstrates Envoy's request mirroring capability using
`request mirror policies <https://www.envoyproxy.io/docs/envoy/latest/api-v3/config/route/v3/route_components.proto#envoy-v3-api-msg-config-route-v3-routeaction-requestmirrorpolicy>`__.

Incoming requests are received by ``envoy-front-proxy`` service.

Requests for the path ``/service/1`` are statically mirrored.

Each request is handled by the ``service1`` cluster, and in addition, forwarded to
the ``service1-mirror`` cluster:

.. literalinclude:: _include/route-mirror/front-envoy.yaml
   :language: yaml
   :lines: 16-34
   :linenos:
   :emphasize-lines: 6-11
   :caption: Envoy configuration with static route mirror policy :download:`front-envoy.yaml <_include/route-mirror/front-envoy.yaml>`

Requests for the path ``/service/2`` are dynamically mirrored according to the presence and value of
the ``x-mirror-cluster`` header.

All reqests for this path are forwarded to the ``service2`` cluster, and are also mirrored
to the cluster named in the header.

For example, if we send a request with the header ``x-mirror-cluster: service2-mirror``,
the request will be forwarded to the ``service2-mirror`` cluster.

.. literalinclude:: _include/route-mirror/front-envoy.yaml
   :language: yaml
   :lines: 16-34
   :linenos:
   :emphasize-lines: 12-17
   :caption: Envoy configuration with header based route mirror policy :download:`front-envoy.yaml <_include/route-mirror/front-envoy.yaml>`


.. warning::

   Allowing a request header to determine the cluster that the request is mirrored to is most useful in
   a trusted environment.

   For example, a downstream Envoy instance (or other application acting as a proxy) might
   automatically add this header to requests for processing by an upstream Envoy instance
   configured with request mirror policies.

   If you allow dynamic mirroring according to request header, you may wish to restrict which requests
   can set or proxy the header.

.. note::

   Envoy will only return the response it receives from the primary cluster to the client.

   For this example, responses from ``service1`` and ``service2`` clusters will be sent
   to the client. A response returned by the ``service1-mirror`` or the ``service2-mirror``
   cluster is not sent back to the client.

   This also means that any problems or latency in request processing in the mirror cluster
   don't affect the response received by the client.

Step 1: Build the sandbox
*************************

Change to the ``examples/route-mirror`` directory.

.. code-block:: console

    $ pwd
    envoy/examples/route-mirror
    $ docker-compose build
    $ docker-compose up -d
    $ docker-compose ps
    NAME                               COMMAND                  SERVICE             STATUS              PORTS
    route-mirror-envoy-front-proxy-1   "/docker-entrypoint.…"   envoy-front-proxy   running             0.0.0.0:10000->10000/tcp, :::10000->10000/tcp
    route-mirror-service1-1            "python3 /code/servi…"   service1            running (healthy)
    route-mirror-service1-mirror-1     "python3 /code/servi…"   service1-mirror     running (healthy)
    route-mirror-service2-1            "python3 /code/servi…"   service2            running (healthy)
    route-mirror-service2-mirror-1     "python3 /code/servi…"   service2-mirror     running (healthy)

Step 2: Make a request to the statically mirrored route
*******************************************************

Let's send a request to the ``envoy-front-proxy`` service which forwards the request to
``service1`` and also sends the request to the service 1 mirror, ``service1-mirror``.

.. code-block:: console

  $ curl localhost:10000/service/1
  Hello from behind Envoy (service 1)!

Step 3: View logs for the statically mirrored request
*****************************************************

The logs from the ``service1`` and ``service1-mirror`` services show that
both the ``service1`` and ``service1-mirror`` services received the request made
in Step 2.

You can also see that for the request to the ``service1-mirror``
service, the ``Host`` header was modified by Envoy to have a ``-shadow`` suffix
in the hostname.

.. code-block:: console

   $ docker-compose logs service1
   ...
   Host: localhost:10000
   192.168.80.6 - - [06/Oct/2022 03:56:22] "GET /service/1 HTTP/1.1" 200 -

   $ docker-compose logs service1-mirror
   ...
   Host: localhost-shadow:10000
   192.168.80.6 - - [06/Oct/2022 03:56:22] "GET /service/1 HTTP/1.1" 200 -


Step 4: Make a request to the route mirrored by request header
**************************************************************

In this step, we will see a demonstration where the request specifies via a header, ``x-mirror-cluster``,
the cluster that envoy will mirror the request to.

Let's send a request to the ``envoy-front-proxy`` service which forwards the request to
``service2`` and also mirrors the request to the cluster named, ``service2-mirror``.

.. code-block:: console

  $ curl --header "x-mirror-cluster: service2-mirror" localhost:10000/service/2
  Hello from behind Envoy (service 2)!


Step 5: View logs for the request mirrored by request header
************************************************************

The logs show that both the ``service2`` and ``service2-mirror`` services
got the request.

.. code-block:: console

   $ docker-compose logs service2
   ...
   Host: localhost:10000
   192.168.80.6 - - [06/Oct/2022 03:56:22] "GET /service/2 HTTP/1.1" 200 -

   $ docker-compose logs service2-mirror
   ...
   Host: localhost-shadow:10000
   192.168.80.6 - - [06/Oct/2022 03:56:22] "GET /service/2 HTTP/1.1" 200 -

You can also see that for the request to the ``service2-mirror`` service, the
``Host`` header was modified by Envoy to have a ``-shadow`` suffix in the
hostname.

Step 6: Missing or invalid cluster name in request header
*********************************************************

If you do not specify the ``x-mirror-cluster`` in the request to ``service2``,
or specify an unknown cluster, the request will not be mirrored but will be
handled in the normal way.

.. code-block:: console

  $ curl localhost:10000/service/2
  Hello from behind Envoy (service 2)!

  $ curl --header "x-mirror-cluster: service2-mirror-non-existent" localhost:10000/service/2
  Hello from behind Envoy (service 2)!

View the logs for ``service2`` and ``service2-mirror`` services.

.. code-block:: console

   $ docker-compose logs service2
   ...
   Host: localhost:10000
   192.168.80.6 - - [06/Oct/2022 03:56:22] "GET /service/2 HTTP/1.1" 200 -

   $ docker-compose logs service2-mirror
   # No new logs

.. seealso::

   :ref:`Envoy request mirror policy <envoy_v3_api_msg_config.route.v3.RouteAction.RequestMirrorPolicy>`
    Learn more Envoy's request mirroring policy.
