.. _install_sandboxes_grpc_bridge:

gRPC Bridge
===========

Envoy gRPC
~~~~~~~~~~

The gRPC bridge sandbox is an example usage of Envoy's
:ref:`gRPC bridge filter <config_http_filters_grpc_bridge>`.
Included in the sandbox is a gRPC in memory Key/Value store with a Python HTTP
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

To use the python service and sent gRPC requests::

  $ pwd
  ~/src/envoy/examples/grpc-bridge
  # set a key
  $ docker-compose exec python /client/client.py set foo bar
  setf foo to bar

  # get a key
  $ docker-compose exec python /client/client.py get foo
  bar

Locally building a docker image with an envoy binary
----------------------------------------------------

The following steps guide you through building your own envoy binary, and
putting that in a clean ubuntu container.

**Step 1: Build Envoy**

Using ``lyft/envoy-build`` you will compile envoy.
This image has all software needed to build envoy. From your envoy directory::

  $ pwd
  src/envoy
  $ ./ci/run_envoy_docker.sh './ci/do_ci.sh bazel.dev'

That command will take some time to run because it is compiling an envoy binary and running tests.

For more information on building and different build targets, please refer to :repo:`ci/README.md`.

**Step 2: Build image with only envoy binary**

In this step we'll build an image that only has the envoy binary, and none
of the software used to build it.::

  $ pwd
  src/envoy/
  $ docker build -f ci/Dockerfile-envoy-image -t envoy .

Now you can use this ``envoy`` image to build the any of the sandboxes if you change
the ``FROM`` line in any dockerfile.

This will be particularly useful if you are interested in modifying envoy, and testing
your changes.
