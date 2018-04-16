.. _install_sandboxes_grpc_bridge:

gRPC Bridge
===========

Envoy gRPC
~~~~~~~~~~

The gRPC bridge sandbox is an example usage of Envoy's
:ref:`gRPC bridge filter <config_http_filters_grpc_bridge>`.
Included in the sandbox is a gRPC in-memory Key/Value store with a Python HTTP
client. The Python client makes HTTP/1 requests through the Envoy sidecar
process which are upgraded into HTTP/2 gRPC requests. Response trailers are then
buffered and sent back to the client as a HTTP/1 header payload.

Another Envoy feature demonstrated in this example is Envoy's ability to do authority
base routing via its route configuration.

Building the Go service
~~~~~~~~~~~~~~~~~~~~~~~

To build the Go gRPC service run::

  $ pwd
  envoy/examples/grpc-bridge
  $ script/bootstrap
  $ script/build

Note: ``build`` requires that your Envoy codebase (or a working copy thereof) is in ``$GOPATH/src/github.com/envoyproxy/envoy``.

Docker compose
~~~~~~~~~~~~~~

To run the docker compose file, and set up both the Python and the gRPC containers
run::

  $ pwd
  envoy/examples/grpc-bridge
  $ docker-compose up --build

Sending requests to the Key/Value store
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

To use the Python service and send gRPC requests::

  $ pwd
  envoy/examples/grpc-bridge
  # set a key
  $ docker-compose exec python /client/client.py set foo bar
  setf foo to bar

  # get a key
  $ docker-compose exec python /client/client.py get foo
  bar

  # modify an existing key
  $ docker-compose exec python /client/client.py set foo baz
  setf foo to baz

  # get the modified key
  $ docker-compose exec python /client/client.py get foo
  baz

In the running docker-compose container, you should see the gRPC service printing a record of its activity::

  grpc_1    | 2017/05/30 12:05:09 set: foo = bar
  grpc_1    | 2017/05/30 12:05:12 get: foo
  grpc_1    | 2017/05/30 12:05:18 set: foo = baz
