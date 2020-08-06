.. _install_sandboxes_ext_authz:

External Authorization Filter
=============================

The External Authorization sandbox demonstrates Envoy's :ref:`ext_authz filter <config_http_filters_ext_authz>`
capability to delegate authorization of incoming requests through Envoy to an external services.

While ext_authz can also be employed as a network filter, this sandbox is limited to exhibit
ext_authz HTTP Filter, which supports to call HTTP or gRPC service.

The setup of this sandbox is very similar to front-proxy deployment, however calls to upstream
service behind the proxy will be checked by an external HTTP or gRPC service. In this sandbox,
for every authorized call, the external authorization service adds additional ``x-current-user``
header entry to the original request headers to be forwarded to the upstream service.

Running the Sandbox
~~~~~~~~~~~~~~~~~~~

**Step 1: Install Docker**

Ensure that you have a recent versions of ``docker`` and ``docker-compose``.

A simple way to achieve this is via the `Docker Desktop <https://www.docker.com/products/docker-desktop>`_.

**Step 2: Clone the Envoy repository and start all of our containers**

If you have not cloned the Envoy repository, clone it with ``git clone git@github.com:envoyproxy/envoy``
or ``git clone https://github.com/envoyproxy/envoy.git``.

To build this sandbox example and start the example services, run the following commands::

    $ pwd
    envoy/examples/ext_authz
    $ docker-compose pull
    $ docker-compose up --build -d
    $ docker-compose ps

                   Name                             Command               State                             Ports
    ---------------------------------------------------------------------------------------------------------------------------------------
    ext_authz_ext_authz-grpc-service_1   /app/server -users /etc/us       Up
    ext_authz_ext_authz-http-service_1   docker-entrypoint.sh node        Up
    ext_authz_front-envoy_1              /docker-entrypoint.sh /bin       Up      10000/tcp, 0.0.0.0:8000->8000/tcp, 0.0.0.0:8001->8001/tcp
    ext_authz_upstream-service_1         python3 /app/service/server.py   Up

.. note::
    This sandbox has multiple setup controlled by ``FRONT_ENVOY_YAML`` environment variable which
    points to the effective Envoy configuration to be used. The default value of ``FRONT_ENVOY_YAML``
    can be defined in the ``.env`` file or provided inline when running the ``docker-compose up``
    command. For more information, pease take a look at `environment variables in Compose documentation <https://docs.docker.com/compose/environment-variables>`_.

By default, ``FRONT_ENVOY_YAML`` points to ``config/grpc-service/v3.yaml`` file which bootstraps
front-envoy with ext_authz HTTP filter with gRPC service ``V3`` (this is specified by :ref:`transport_api_version field<envoy_v3_api_field_extensions.filters.http.ext_authz.v3.ExtAuthz.transport_api_version>`).
The possible values of ``FRONT_ENVOY_YAML`` can be found inside the ``envoy/examples/ext_authz/config``
directory.

For example, to run Envoy with ext_authz HTTP filter with HTTP service will be::

    $ pwd
    envoy/examples/ext_authz
    $ docker-compose pull
    $ # Tearing down the currently running setup
    $ docker-compose down
    $ FRONT_ENVOY_YAML=config/http-service.yaml docker-compose up --build -d
    $ # Or you can update the .env file with the above FRONT_ENVOY_YAML value, so you don't have to specify it when running the "up" command.

**Step 3: Access the upstream-service behind the Front Envoy**

You can now try to send a request to upstream-service via the front-envoy as follows::

    $ curl -v localhost:8000/service
    *   Trying 127.0.0.1...
    * TCP_NODELAY set
    * Connected to localhost (127.0.0.1) port 8000 (#0)
    > GET /service HTTP/1.1
    > Host: localhost:8000
    > User-Agent: curl/7.58.0
    > Accept: */*
    >
    < HTTP/1.1 403 Forbidden
    < date: Fri, 19 Jun 2020 15:02:24 GMT
    < server: envoy
    < content-length: 0

As observed, the request failed with ``403 Forbidden`` status code. This happened since the ext_authz
filter employed by Envoy rejected the call. To let the request reach the upstream service, you need
to provide a ``Bearer`` token via the ``Authorization`` header.

.. note::
    A complete list of users is defined in ``envoy/examples/ext_authz/auth/users.json`` file. For
    example, the ``token1`` used in the below example is corresponding to ``user1``.

An example of successful requests can be observed as follows::

    $ curl -v -H "Authorization: Bearer token1" localhost:8000/service
    *   Trying 127.0.0.1...
    * TCP_NODELAY set
    * Connected to localhost (127.0.0.1) port 8000 (#0)
    > GET /service HTTP/1.1
    > Host: localhost:8000
    > User-Agent: curl/7.58.0
    > Accept: */*
    > Authorization: Bearer token1
    >
    < HTTP/1.1 200 OK
    < content-type: text/html; charset=utf-8
    < content-length: 24
    < server: envoy
    < date: Fri, 19 Jun 2020 15:04:29 GMT
    < x-envoy-upstream-service-time: 2
    <
    * Connection #0 to host localhost left intact
    Hello user1 from behind Envoy!

We can also employ `Open Policy Agent <https://www.openpolicyagent.org/>`_ server
(with `envoy_ext_authz_grpc <https://github.com/open-policy-agent/opa-istio-plugin>`_ plugin enabled)
as the authorization server. To run this example::

    $ pwd
    envoy/examples/ext_authz
    $ docker-compose pull
    $ # Tearing down the currently running setup
    $ docker-compose down
    $ FRONT_ENVOY_YAML=config/opa-service/v2.yaml docker-compose up --build -d

And sending a request to the upstream service (via the Front Envoy) gives::

    $ curl localhost:8000/service --verbose
    *   Trying ::1...
    * TCP_NODELAY set
    * Connected to localhost (::1) port 8000 (#0)
    > GET /service HTTP/1.1
    > Host: localhost:8000
    > User-Agent: curl/7.64.1
    > Accept: */*
    >
    < HTTP/1.1 200 OK
    < content-type: text/html; charset=utf-8
    < content-length: 28
    < server: envoy
    < date: Thu, 02 Jul 2020 06:29:58 GMT
    < x-envoy-upstream-service-time: 2
    <
    * Connection #0 to host localhost left intact
    Hello OPA from behind Envoy!

From the logs, we can observe the policy decision message from the Open Policy Agent server (for
the above request against the defined policy in ``config/opa-service/policy.rego``)::

    $ docker-compose logs ext_authz-opa-service | grep decision_id -A 30
    ext_authz-opa-service_1   |   "decision_id": "8143ca68-42d8-43e6-ade6-d1169bf69110",
    ext_authz-opa-service_1   |   "input": {
    ext_authz-opa-service_1   |     "attributes": {
    ext_authz-opa-service_1   |       "destination": {
    ext_authz-opa-service_1   |         "address": {
    ext_authz-opa-service_1   |           "Address": {
    ext_authz-opa-service_1   |             "SocketAddress": {
    ext_authz-opa-service_1   |               "PortSpecifier": {
    ext_authz-opa-service_1   |                 "PortValue": 8000
    ext_authz-opa-service_1   |               },
    ext_authz-opa-service_1   |               "address": "172.28.0.6"
    ext_authz-opa-service_1   |             }
    ext_authz-opa-service_1   |           }
    ext_authz-opa-service_1   |         }
    ext_authz-opa-service_1   |       },
    ext_authz-opa-service_1   |       "metadata_context": {},
    ext_authz-opa-service_1   |       "request": {
    ext_authz-opa-service_1   |         "http": {
    ext_authz-opa-service_1   |           "headers": {
    ext_authz-opa-service_1   |             ":authority": "localhost:8000",
    ext_authz-opa-service_1   |             ":method": "GET",
    ext_authz-opa-service_1   |             ":path": "/service",
    ext_authz-opa-service_1   |             "accept": "*/*",
    ext_authz-opa-service_1   |             "user-agent": "curl/7.64.1",
    ext_authz-opa-service_1   |             "x-forwarded-proto": "http",
    ext_authz-opa-service_1   |             "x-request-id": "b77919c0-f1d4-4b06-b444-5a8b32d5daf4"
    ext_authz-opa-service_1   |           },
    ext_authz-opa-service_1   |           "host": "localhost:8000",
    ext_authz-opa-service_1   |           "id": "16617514055874272263",
    ext_authz-opa-service_1   |           "method": "GET",
    ext_authz-opa-service_1   |           "path": "/service",

Trying to send a request with method other than ``GET`` gives a rejection::

    $ curl -X POST localhost:8000/service --verbose
    *   Trying ::1...
    * TCP_NODELAY set
    * Connected to localhost (::1) port 8000 (#0)
    > PUT /service HTTP/1.1
    > Host: localhost:8000
    > User-Agent: curl/7.64.1
    > Accept: */*
    >
    < HTTP/1.1 403 Forbidden
    < date: Thu, 02 Jul 2020 06:46:13 GMT
    < server: envoy
    < content-length: 0
