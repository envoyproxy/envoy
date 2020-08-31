.. _install_sandboxes_cache_filter:

Cache Filter
============
.. TODO(yosrym93): When a documentation is written for a production-ready Cache Filter, link to it through this doc.

In this example, we demonstrate how HTTP caching can be utilized in Envoy by using the Cache Filter. 
The setup of this sandbox is based on the setup of the :ref:`Front Proxy sandbox <install_sandboxes_front_proxy>`.

All incoming requests are routed via the front Envoy, which is acting as a reverse proxy sitting on
the edge of the ``envoymesh`` network. Ports ``8000`` and ``8001`` are exposed by docker
compose (see :repo:`/examples/cache/docker-compose.yaml`) to handle ``HTTP`` calls
to the services, and requests to ``/admin`` respectively. Two backend services are deployed behind the front Envoy, each with a sidecar Envoy.

The front Envoy is configured to run the Cache Filter, which will store cacheable responses in an in-memory cache, 
and serve it to subsequent requests. In this demo, the responses that are served by the deployed services are stored in :repo:`/examples/cache/responses.yaml`. 
This file is mounted to both services containers, so any changes made to the stored responses while the services are running should be instantly effective (no need to rebuild or rerun). 
For the purposes of the demo, a response's date of creation is appended to its body before being served. 
An Etag is computed for every response for validation purposes, which only depends on the response body in the yaml file (i.e the appended date is not taken into account). 
Cached responses can be identified by having an ``age`` header. Validated responses can be identified by having a generation date older than the ``date`` header;
as when a response is validated the ``date`` header is updated, while the body stays the same.

Running the Sandbox
~~~~~~~~~~~~~~~~~~~

The following documentation runs through the setup of an Envoy cluster organized
as is described in the image above.

**Step 1: Install Docker**

Ensure that you have a recent versions of ``docker`` and ``docker-compose`` installed.

A simple way to achieve this is via the `Docker Desktop <https://www.docker.com/products/docker-desktop>`_.

**Step 2: Clone the Envoy repo**

If you have not cloned the Envoy repo, clone it with:

``git clone git@github.com:envoyproxy/envoy``

or

``git clone https://github.com/envoyproxy/envoy.git``

**Step 3: Start all of our containers**

.. code-block:: console

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

You can now send a request to both services via the ``front-envoy``. Note that since the two services have different routes,
identical requests to different services have different cache entries (i.e a request sent to service 2 will not be served by a cached
response produced by service 1).

To send a request: 

``curl -v localhost:8000/service/<service_no>/<response>``

``service_no``: The service to send the request to, 1 or 2.

``response``: The response that is being requested. The responses are found in :repo:`/examples/cache/responses.yaml`.

You can change the responses headers and bodies (or add new ones) while the sandbox is running to experiment.

Example responses
^^^^^^^^^^^^^^^^^^^^^
1. A response that stays fresh for a minute:

.. code-block:: console

    $ curl -v localhost:8000/service/1/valid-for-minute
    *   Trying ::1:8000...
    * TCP_NODELAY set
    * Connected to localhost (::1) port 8000 (#0)
    > GET /service/2/valid-for-minute HTTP/1.1
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
    < date: Mon, 31 Aug 2020 18:57:40 GMT
    < server: envoy
    < x-envoy-upstream-service-time: 12
    < 
    This response will stay fresh for one minute
    Response body generated at: Mon, 31 Aug 2020 18:57:40 GMT
    * Connection #0 to host localhost left intact

Naturally, response ``date`` header is the same time as the generated time.
Sending the same request after 30 seconds gives the same exact response with the same generation date, 
but with an ``age`` header as it was served from cache:

.. code-block:: console

    $ curl -v localhost:8000/service/1/valid-for-minute
    *   Trying ::1:8000...
    * TCP_NODELAY set
    * Connected to localhost (::1) port 8000 (#0)
    > GET /service/2/valid-for-minute HTTP/1.1
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
    < date: Mon, 31 Aug 2020 18:57:40 GMT
    < server: envoy
    < x-envoy-upstream-service-time: 12
    < age: 30
    < 
    This response will stay fresh for one minute
    Response body generated at: Mon, 31 Aug 2020 18:57:40 GMT
    * Connection #0 to host localhost left intact

After 1 minute and 1 second:

.. code-block:: console

    $ curl -v localhost:8000/service/1/valid-for-minute
    *   Trying ::1:8000...
    * TCP_NODELAY set
    * Connected to localhost (::1) port 8000 (#0)
    > GET /service/2/valid-for-minute HTTP/1.1
    > Host: localhost:8000
    > User-Agent: curl/7.68.0
    > Accept: */*
    > 
    * Mark bundle as not supporting multiuse
    < HTTP/1.1 200 OK
    < cache-control: max-age=60
    < custom-header: any value
    < etag: "172ae25df822c3299cf2248694b4ce23"
    < date: Mon, 31 Aug 2020 18:58:41 GMT
    < server: envoy
    < x-envoy-upstream-service-time: 8
    < content-length: 103
    < content-type: text/html; charset=utf-8
    < 
    This response will stay fresh for one minute
    Response body generated at: Mon, 31 Aug 2020 18:57:40 GMT
    * Connection #0 to host localhost left intact

The same response was served after being validated with the backend service. 
You can see this as the response generation time is the same, 
but the response ``date`` header was updated with the validation response date. 
Also, no ``age`` header.

2. A private response:

.. code-block:: console

    curl -v localhost:8000/service/1/private
    *   Trying ::1:8000...
    * TCP_NODELAY set
    * Connected to localhost (::1) port 8000 (#0)
    > GET /service/1/private HTTP/1.1
    > Host: localhost:8000
    > User-Agent: curl/7.68.0
    > Accept: */*
    > 
    * Mark bundle as not supporting multiuse
    < HTTP/1.1 200 OK
    < content-type: text/html; charset=utf-8
    < content-length: 117
    < cache-control: private
    < etag: "6bd80b59b2722606abf2b8d83ed2126d"
    < date: Mon, 31 Aug 2020 19:05:59 GMT
    < server: envoy
    < x-envoy-upstream-service-time: 7
    < 
    This is a private response, it will not be cached by Envoy
    Response body generated at: Mon, 31 Aug 2020 19:05:59 GMT
    * Connection #0 to host localhost left intact

No matter how many times you make this request, you will always receive a new response; 
new date of generation, new ``date`` header, and no ``age`` header. 
This is because private response cannot be cached by shared caches such as proxies.

3. A response that must always be validated:

.. code-block:: console

    curl -v localhost:8000/service/1/no-cache
    *   Trying ::1:8000...
    * TCP_NODELAY set
    * Connected to localhost (::1) port 8000 (#0)
    > GET /service/1/no-cache HTTP/1.1
    > Host: localhost:8000
    > User-Agent: curl/7.68.0
    > Accept: */*
    > 
    * Mark bundle as not supporting multiuse
    < HTTP/1.1 200 OK
    < cache-control: max-age=0, no-cache
    < etag: "ce39a53bd6bb8abdb2488a5a375397e4"
    < date: Mon, 31 Aug 2020 19:07:16 GMT
    < server: envoy
    < x-envoy-upstream-service-time: 8
    < content-length: 130
    < content-type: text/html; charset=utf-8
    < 
    This response can be cached, but it has to be validated on each request
    Response body generated at: Mon, 31 Aug 2020 18:48:42 GMT
    * Connection #0 to host localhost left intact

After a few seconds:

.. code-block:: console

    curl -v localhost:8000/service/1/no-cache
    *   Trying ::1:8000...
    * TCP_NODELAY set
    * Connected to localhost (::1) port 8000 (#0)
    > GET /service/1/no-cache HTTP/1.1
    > Host: localhost:8000
    > User-Agent: curl/7.68.0
    > Accept: */*
    > 
    * Mark bundle as not supporting multiuse
    < HTTP/1.1 200 OK
    < cache-control: max-age=0, no-cache
    < etag: "ce39a53bd6bb8abdb2488a5a375397e4"
    < date: Mon, 31 Aug 2020 19:07:22 GMT
    < server: envoy
    < x-envoy-upstream-service-time: 7
    < content-length: 130
    < content-type: text/html; charset=utf-8
    < age: 6
    < 
    Response body generated at: Mon, 31 Aug 2020 18:48:42 GMT
    * Connection #0 to host localhost left intact

You will receive a cached response, identified by the ``age`` header and the same generation time. 
However, the ``date`` header will always be updated as this response will always be validated first.

If you change the response body in the yaml file:

.. code-block:: console

    curl -v localhost:8000/service/1/no-cache
    *   Trying ::1:8000...
    * TCP_NODELAY set
    * Connected to localhost (::1) port 8000 (#0)
    > GET /service/1/no-cache HTTP/1.1
    > Host: localhost:8000
    > User-Agent: curl/7.68.0
    > Accept: */*
    > 
    * Mark bundle as not supporting multiuse
    < HTTP/1.1 200 OK
    < content-type: text/html; charset=utf-8
    < content-length: 133
    < cache-control: max-age=0, no-cache
    < etag: "f4768af0ac9f6f54f88169a1f3ecc9f3"
    < date: Mon, 31 Aug 2020 19:10:25 GMT
    < server: envoy
    < x-envoy-upstream-service-time: 8
    < 
    This response can be cached, but it has to be validated on each request!!!
    Response body generated at: Mon, 31 Aug 2020 19:10:2

You will receive a new response that's served from the backend.

You can also add new responses to the yaml file with different ``cache-control`` headers and start experimenting!
Different ``cache-control`` headers can be found in 
the `MDN Web Docs <https://developer.mozilla.org/en-US/docs/Web/HTTP/Headers/Cache-Control>`_.