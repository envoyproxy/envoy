.. _install_sandboxes_local_docker_build:

Building an Envoy Docker image
==============================

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
