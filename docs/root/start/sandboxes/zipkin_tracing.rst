.. _install_sandboxes_zipkin_tracing:

Zipkin Tracing
==============

The Zipkin tracing sandbox demonstrates Envoy's :ref:`request tracing <arch_overview_tracing>`
capabilities using `Zipkin <https://zipkin.io/>`_ as the tracing provider. This sandbox
is very similar to the front proxy architecture described above, with one difference:
service1 makes an API call to service2 before returning a response.
The three containers will be deployed inside a virtual network called ``envoymesh``.

All incoming requests are routed via the front Envoy, which is acting as a reverse proxy
sitting on the edge of the ``envoymesh`` network. Port ``8000`` is exposed
by docker compose (see :repo:`/examples/zipkin-tracing/docker-compose.yaml`). Notice that
all Envoys are configured to collect request traces (e.g., http_connection_manager/config/tracing setup in
:repo:`/examples/zipkin-tracing/front-envoy-zipkin.yaml`) and setup to propagate the spans generated
by the Zipkin tracer to a Zipkin cluster (trace driver setup
in :repo:`/examples/zipkin-tracing/front-envoy-zipkin.yaml`).

Before routing a request to the appropriate service Envoy or the application, Envoy will take
care of generating the appropriate spans for tracing (parent/child/shared context spans).
At a high-level, each span records the latency of upstream API calls as well as information
needed to correlate the span with other related spans (e.g., the trace ID).

One of the most important benefits of tracing from Envoy is that it will take care of
propagating the traces to the Zipkin service cluster. However, in order to fully take advantage
of tracing, the application has to propagate trace headers that Envoy generates, while making
calls to other services. In the sandbox we have provided, the simple flask app
(see trace function in :repo:`/examples/front-proxy/service.py`) acting as service1 propagates
the trace headers while making an outbound call to service2.


Running the Sandbox
~~~~~~~~~~~~~~~~~~~

The following documentation runs through the setup of an Envoy cluster organized
as is described above.

**Step 1: Install Docker**

Ensure that you have a recent versions of ``docker`` and ``docker-compose``.

A simple way to achieve this is via the `Docker Desktop <https://www.docker.com/products/docker-desktop>`_.

**Step 2: Clone the Envoy repo**

If you have not cloned the Envoy repo, clone it with:

``git clone git@github.com:envoyproxy/envoy``

or

``git clone https://github.com/envoyproxy/envoy.git``

**Step 3: Build the sandbox**

To build this sandbox example, and start the example apps run the following commands:

.. code-block:: console

    $ pwd
    envoy/examples/zipkin-tracing
    $ docker-compose pull
    $ docker-compose up --build -d
    $ docker-compose ps

                Name                          Command             State                            Ports
    -----------------------------------------------------------------------------------------------------------------------------
    zipkin-tracing_front-envoy_1   /docker-entrypoint.sh /bin ... Up      10000/tcp, 0.0.0.0:8000->8000/tcp, 0.0.0.0:8001->8001/tcp
    zipkin-tracing_service1_1      /bin/sh -c /usr/local/bin/ ... Up      10000/tcp, 8000/tcp
    zipkin-tracing_service2_1      /bin/sh -c /usr/local/bin/ ... Up      10000/tcp, 8000/tcp
    zipkin-tracing_zipkin_1        /busybox/sh run.sh             Up      9410/tcp, 0.0.0.0:9411->9411/tcp

**Step 4: Generate some load**

You can now send a request to service1 via the front-envoy as follows:

.. code-block:: console

    $ curl -v localhost:8000/trace/1
    *   Trying 192.168.99.100...
    * Connected to 192.168.99.100 (192.168.99.100) port 8000 (#0)
    > GET /trace/1 HTTP/1.1
    > Host: 192.168.99.100:8000
    > User-Agent: curl/7.43.0
    > Accept: */*
    >
    < HTTP/1.1 200 OK
    < content-type: text/html; charset=utf-8
    < content-length: 89
    < x-envoy-upstream-service-time: 1
    < server: envoy
    < date: Fri, 26 Aug 2018 19:39:19 GMT
    <
    Hello from behind Envoy (service 1)! hostname: f26027f1ce28 resolvedhostname: 172.19.0.6
    * Connection #0 to host 192.168.99.100 left intact

**Step 5: View the traces in Zipkin UI**

Point your browser to http://localhost:9411 . You should see the Zipkin dashboard.
Set the service to "front-proxy" and set the start time to a few minutes before
the start of the test (step 2) and hit enter. You should see traces from the front-proxy.
Click on a trace to explore the path taken by the request from front-proxy to service1
to service2, as well as the latency incurred at each hop.
