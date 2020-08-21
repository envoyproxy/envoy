.. _install_sandboxes_cache_filter:

Cache Filter
===========
.. TODO(yosrym93): When a documentation is written for a production-ready Cache Filter, link to it through this doc.

In this example, we demonstrate how HTTP caching can be utilized in Envoy by using the Cache Filter. The setup of this sandbox is based on the setup of the :ref:`Front Proxy sandbox <_install_sandboxes_front_proxy>`.

All incoming requests are routed via the front Envoy, which is acting as a reverse proxy sitting on
the edge of the ``envoymesh`` network. Ports ``8000`` and ``8001`` are exposed by docker
compose (see :repo:`/examples/cache/docker-compose.yaml`) to handle ``HTTP`` calls
to the services, and requests to ``/admin`` respectively. Two backend services are deployed behind the front Envoy, each with a sidecar Envoy.

The front Envoy is configured to run the Cache Filter, which will store cacheable responses in a proof-of-concept in-memory SimpleHttpCache, 
and serve it to subsequent requests. In this demo, the responses that are served by the deployed services are stored in :repo:`/examples/cache/responses.yaml`. This file is mounted to both services containers, so any changes made to the stored responses while the services are running should be instantly effective (no need to rebuild or rerun). For the purposes of the demo, the responses' date of creation is appended to the body before being served.  Also, when cached response validation takes place, a custom header is added to the response, which can either be ``validation: not-modified`` or ``validation: new-response``, to show if the backend service actually validated the cached response and the validation results. Cached responses can be identified by having an ``age`` header (validated responses do not have an ``age`` header).

Running the Sandbox
~~~~~~~~~~~~~~~~~~~

The following documentation runs through the setup of an Envoy cluster organized
as is described in the image above.

**Step 1: Install Docker**

Ensure that you have a recent versions of ``docker`` and ``docker-compose`` installed.

A simple way to achieve this is via the `Docker Desktop <https://www.docker.com/products/docker-desktop>`_.

**Step 2: Clone the Envoy repo, and start all of our containers**

If you have not cloned the Envoy repo, clone it with ``git clone git@github.com:envoyproxy/envoy``
or ``git clone https://github.com/envoyproxy/envoy.git``::

    $ pwd
    envoy/examples/cache
    $ docker-compose build --pull
    $ docker-compose up -d
    $ docker-compose ps

           Name                      Command               State                             Ports                          
    ------------------------------------------------------------------------------------------------------------------------
    cache_front-envoy_1   /docker-entrypoint.sh /bin ...   Up      10000/tcp, 0.0.0.0:8000->8000/tcp, 0.0.0.0:8001->8001/tcp
    cache_service1_1      /bin/sh -c /usr/local/bin/ ...   Up      10000/tcp, 8000/tcp                                      
    cache_service2_1      /bin/sh -c /usr/local/bin/ ...   Up      10000/tcp, 8000/tcp    

**Step 3: Test Envoy's HTTP caching capabilities**

.. TODO(yosrym93): Complete the doc starting here.

You can now send a request to both services via the ``front-envoy``. Note that since the two services have different routes,
identical requests to different services have different cache entries (i.e a request sent to service 2 will not be served by a cached
response produced by service 1).

To send a request: 

``curl -v localhost:8000/service/<service_no>/<response>``

``service_no``: The service to send the request to, 1 or 2.

``response``: The response that is being requested. The responses are found in :repo:`/examples/cache/responses.yaml`.

You can change the responses headers and bodies (or add new ones) while the sandbox is running to experiment.

Examples

A response that stays fresh for one minute::

    $ curl -v localhost:8000/service/1/valid-for-minute
    *   Trying ::1:8000...
    * TCP_NODELAY set
    * Connected to localhost (::1) port 8000 (#0)
    > GET /service/1/valid-for-minute HTTP/1.1
    > Host: localhost:8000
    > User-Agent: curl/7.68.0
    > Accept: */*
    > 
    * Mark bundle as not supporting multiuse
    < HTTP/1.1 200 OK
    < content-type: text/html; charset=utf-8
    < content-length: 103
    < cache-control: max-age=60
    < custom-header: any value
    < etag: "172ae25df822c3299cf2248694b4ce23"
    < date: Wed, 26 Aug 2020 00:42:27 GMT
    < server: envoy
    < x-envoy-upstream-service-time: 12
    < 
    This response will stay fresh for one minute
    Response body generated at: Wed, 26 Aug 2020 00:42:27 GMT

After 30 seconds:
