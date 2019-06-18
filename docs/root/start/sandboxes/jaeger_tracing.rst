.. _install_sandboxes_jaeger_tracing:

Jaeger Tracing
==============

The Jaeger tracing sandbox demonstrates Envoy's :ref:`request tracing <arch_overview_tracing>`
capabilities using `Jaeger <https://jaegertracing.io/>`_ as the tracing provider. This sandbox
is very similar to the front proxy architecture described above, with one difference:
service1 makes an API call to service2 before returning a response.
The three containers will be deployed inside a virtual network called ``envoymesh``.

All incoming requests are routed via the front Envoy, which is acting as a reverse proxy
sitting on the edge of the ``envoymesh`` network. Port ``80`` is mapped to  port ``8000``
by docker compose (see :repo:`/examples/jaeger-tracing/docker-compose.yaml`). Notice that
all Envoys are configured to collect request traces (e.g., http_connection_manager/config/tracing setup in
:repo:`/examples/jaeger-tracing/front-envoy-jaeger.yaml`) and setup to propagate the spans generated
by the Jaeger tracer to a Jaeger cluster (trace driver setup
in :repo:`/examples/jaeger-tracing/front-envoy-jaeger.yaml`).

Before routing a request to the appropriate service Envoy or the application, Envoy will take
care of generating the appropriate spans for tracing (parent/child context spans).
At a high-level, each span records the latency of upstream API calls as well as information
needed to correlate the span with other related spans (e.g., the trace ID).

One of the most important benefits of tracing from Envoy is that it will take care of
propagating the traces to the Jaeger service cluster. However, in order to fully take advantage
of tracing, the application has to propagate trace headers that Envoy generates, while making
calls to other services. In the sandbox we have provided, the simple flask app
(see trace function in :repo:`/examples/front-proxy/service.py`) acting as service1 propagates
the trace headers while making an outbound call to service2.


Running the Sandbox
~~~~~~~~~~~~~~~~~~~

The following documentation runs through the setup of an Envoy cluster organized
as is described in the image above.

**Step 1: Build the sandbox**

To build this sandbox example, and start the example apps run the following commands::

    $ pwd
    envoy/examples/jaeger-tracing
    $ docker-compose pull
    $ docker-compose up --build -d
    $ docker-compose ps

                Name                          Command             State                                                       Ports
    ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
    jaeger-tracing_front-envoy_1   /docker-entrypoint.sh /bin ... Up      10000/tcp, 0.0.0.0:8000->80/tcp, 0.0.0.0:8001->8001/tcp
    jaeger-tracing_jaeger_1        /go/bin/all-in-one-linux - ... Up      14250/tcp, 14268/tcp, 0.0.0.0:16686->16686/tcp, 5775/udp, 5778/tcp, 6831/udp, 6832/udp, 0.0.0.0:9411->9411/tcp
    jaeger-tracing_service1_1      /bin/sh -c /usr/local/bin/ ... Up      10000/tcp, 80/tcp
    jaeger-tracing_service2_1      /bin/sh -c /usr/local/bin/ ... Up      10000/tcp, 80/tcp

**Step 2: Generate some load**

You can now send a request to service1 via the front-envoy as follows::

    $ curl -v localhost:8000/trace/1
    *   Trying 192.168.99.100...
    * Connected to 192.168.99.100 (192.168.99.100) port 8000 (#0)
    > GET /trace/1 HTTP/1.1
    > Host: 192.168.99.100:8000
    > User-Agent: curl/7.54.0
    > Accept: */*
    >
    < HTTP/1.1 200 OK
    < content-type: text/html; charset=utf-8
    < content-length: 89
    < x-envoy-upstream-service-time: 9
    < server: envoy
    < date: Fri, 26 Aug 2018 19:39:19 GMT
    <
    Hello from behind Envoy (service 1)! hostname: f26027f1ce28 resolvedhostname: 172.19.0.6
    * Connection #0 to host 192.168.99.100 left intact

**Step 3: View the traces in Jaeger UI**

Point your browser to http://localhost:16686 . You should see the Jaeger dashboard.
Set the service to "front-proxy" and hit 'Find Traces'. You should see traces from the front-proxy.
Click on a trace to explore the path taken by the request from front-proxy to service1
to service2, as well as the latency incurred at each hop.
