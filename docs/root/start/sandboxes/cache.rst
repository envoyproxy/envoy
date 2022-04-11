.. _install_sandboxes_cache_filter:

Cache filter
============
.. TODO(yosrym93): When a documentation is written for a production-ready Cache Filter, link to it through this doc.

.. sidebar:: Requirements

   .. include:: _include/docker-env-setup-link.rst

   :ref:`curl <start_sandboxes_setup_curl>`
        Used to make ``HTTP`` requests.

In this example, we demonstrate how HTTP caching can be utilized in Envoy by using the Cache Filter.

All incoming requests are routed via the front Envoy, which acts as a reverse proxy.

Port ``8000`` is exposed by :download:`docker-compose.yaml <_include/cache/docker-compose.yaml>` to handle ``HTTP`` calls
to the service.

The front Envoy is configured to run the Cache Filter, which stores cacheable responses in an in-memory cache,
and serves it to subsequent requests.

In this demo, the responses that are served are defined in :download:`responses.yaml <_include/cache/responses.yaml>`.

For the purposes of the demo, a response's date of creation is appended to its body before being served.

An Etag is computed for every response for validation purposes, which only depends on the response body in the yaml file (i.e. the appended date is not taken into account).

Cached responses can be identified by having an ``age`` header.

Validated responses can be identified by having a generation date older than the ``date`` header.

When a response is validated, the ``date`` header is updated, while the body stays the same.

Validated responses do not have an ``age`` header.

Responses served from the backend service have no ``age`` header, and their ``date`` header is the same as their generation date.

Three service endpoints are provided:

- ``valid-for-minute``
    This response remains fresh in the cache for a minute. After which, the response gets validated by the backend service before being served from the cache.
    If found to be updated, the new response is served (and cached). Otherwise, the cached response is served and refreshed.

- ``private``
    This response is private; it cannot be stored by shared caches (such as proxies). It will always be served from the backend service.

- ``no-cache``
    This response has to be validated every time before being served.

Only the first type of request will cache responses.

Step 1: Start the containers
****************************

Change to the ``examples/cache`` directory.

.. code-block:: console

    $ pwd
    envoy/examples/cache
    $ docker-compose build --pull
    $ docker-compose up -d
    $ docker-compose ps

           Name                      Command            State           Ports
    ----------------------------------------------------------------------------------------------
    cache_front-envoy_1   /docker-entrypoint.sh /bin ... Up       10000/tcp, 0.0.0.0:8000->8000/tcp,:::8000->8000/tcp
    cache_service_1       python3 /code/service.py       Up

Step 2: Test Envoy's HTTP cached response
*****************************************

To test Envoy caches the

.. code-block:: console

    $ curl -i localhost:8000/valid-for-minute
    HTTP/1.1 200 OK
    content-type: text/html; charset=utf-8
    content-length: 103
    cache-control: max-age=60
    custom-header: any value
    etag: "172ae25df822c3299cf2248694b4ce23"
    date: Fri, 11 Sep 2020 03:20:40 GMT
    server: envoy
    x-envoy-upstream-service-time: 11

    This response will stay fresh for one minute
    Response body generated at: Fri, 11 Sep 2020 03:20:40 GMT

For this first request, the response ``date`` header is the same time as the generated time.

Sending the same request after 30 seconds gives the same response and with the same generation date,
but with an ``age`` header indicating that it was served from Envoy's cache:

.. code-block:: console

    $ curl -i localhost:8000/valid-for-minute
    HTTP/1.1 200 OK
    content-type: text/html; charset=utf-8
    content-length: 103
    cache-control: max-age=60
    custom-header: any value
    etag: "172ae25df822c3299cf2248694b4ce23"
    date: Fri, 11 Sep 2020 03:20:40 GMT
    server: envoy
    x-envoy-upstream-service-time: 11
    age: 30

    This response will stay fresh for one minute
    Response body generated at: Fri, 11 Sep 2020 03:20:40 GMT

After 1 minute and 1 second:

.. code-block:: console

    $ curl -i localhost:8000/service/1/valid-for-minute
    HTTP/1.1 200 OK
    cache-control: max-age=60
    custom-header: any value
    etag: "172ae25df822c3299cf2248694b4ce23"
    date: Fri, 11 Sep 2020 03:21:41 GMT
    server: envoy
    x-envoy-upstream-service-time: 8
    content-length: 103
    content-type: text/html; charset=utf-8

    This response will stay fresh for one minute
    Response body generated at: Fri, 11 Sep 2020 03:20:40 GMT

The same response was served after being validated with the backend service.
You can verify this as the response generation time is the same,
but the response ``date`` header was updated with the validation response date.
Also, no ``age`` header.

Every time the response is validated, it stays fresh for another minute.
If the response body changes while the cached response is still fresh,
the cached response will still be served. The cached response will only be updated when it is no longer fresh.


Step 2: Test Envoy does not cache ``private`` responses
*******************************************************

The second request type - ``private`` adds a ``cache-control: private`` header to the response.

.. code-block:: console

    $ curl -i localhost:8000/service/1/private
    HTTP/1.1 200 OK
    content-type: text/html; charset=utf-8
    content-length: 117
    cache-control: private
    etag: "6bd80b59b2722606abf2b8d83ed2126d"
    date: Fri, 11 Sep 2020 03:22:28 GMT
    server: envoy
    x-envoy-upstream-service-time: 7

    This is a private response, it will not be cached by Envoy
    Response body generated at: Fri, 11 Sep 2020 03:22:28 GMT

No matter how many times you make this request, you will always receive a new response;
new date of generation, new ``date`` header, and no ``age`` header.

Step 3: Test Envoy does not cache ``no-cache`` responses
********************************************************

.. code-block:: console

    $ curl -i localhost:8000/service/1/no-cache
    HTTP/1.1 200 OK
    content-type: text/html; charset=utf-8
    content-length: 130
    cache-control: max-age=0, no-cache
    etag: "ce39a53bd6bb8abdb2488a5a375397e4"
    date: Fri, 11 Sep 2020 03:23:07 GMT
    server: envoy
    x-envoy-upstream-service-time: 7

    This response can be cached, but it has to be validated on each request
    Response body generated at: Fri, 11 Sep 2020 03:23:07 GMT

After a few seconds:

.. code-block:: console

    $ curl -i localhost:8000/service/1/no-cache
    HTTP/1.1 200 OK
    cache-control: max-age=0, no-cache
    etag: "ce39a53bd6bb8abdb2488a5a375397e4"
    date: Fri, 11 Sep 2020 03:23:12 GMT
    server: envoy
    x-envoy-upstream-service-time: 7
    content-length: 130
    content-type: text/html; charset=utf-8

    This response can be cached, but it has to be validated on each request
    Response body generated at: Fri, 11 Sep 2020 03:23:07 GMT

You will receive a cached response that has the same generation time.
However, the ``date`` header will always be updated as this response will always be validated first.
Also, no ``age`` header.

If you change the response body in the yaml file:

.. code-block:: console

    $ curl -i localhost:8000/service/1/no-cache
    HTTP/1.1 200 OK
    content-type: text/html; charset=utf-8
    content-length: 133
    cache-control: max-age=0, no-cache
    etag: "f4768af0ac9f6f54f88169a1f3ecc9f3"
    date: Fri, 11 Sep 2020 03:24:10 GMT
    server: envoy
    x-envoy-upstream-service-time: 7

    This response can be cached, but it has to be validated on each request!!!
    Response body generated at: Fri, 11 Sep 2020 03:24:10 GMT

You will receive a new response that's served from the backend service.
The new response will be cached for subsequent requests.

You can also add new responses to the yaml file with different ``cache-control`` headers and start experimenting!

.. seealso::

   `MDN Web Docs <https://developer.mozilla.org/en-US/docs/Web/HTTP/Caching>`_.
      Learn more about caching and ``cache-control`` on the web.
