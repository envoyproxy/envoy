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
  ~/src/envoy/examples/grpc-bridge
  $ script/bootstrap
  $ script/build

Docker compose
~~~~~~~~~~~~~~

To run the docker compose file, and set up both the Python and the gRPC containers
run::

  $ pwd
  ~/src/envoy/examples/grpc-bridge
  $ docker-compose up --build

Sending requests to the Key/Value store
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

To use the Python service and send gRPC requests::

  $ pwd
  ~/src/envoy/examples/grpc-bridge
  # set a key
  $ docker-compose exec python /client/client.py set foo bar
  setf foo to bar

  # get a key
  $ docker-compose exec python /client/client.py get foo
  bar
