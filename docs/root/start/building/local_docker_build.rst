
.. _install_sandboxes_local_docker_build:

Building an Envoy Docker image
==============================

The following steps guide you through building your own Envoy binary, and
putting that in a clean Ubuntu container.

.. tip::
   These instructions run commands in Docker using ``ci/run_envoy_docker.sh``.

   By default this will place bazel run files and any artefacts in ``/tmp/envoy-docker-build``.

   You can override this by setting the ``ENVOY_DOCKER_BUILD_DIR`` env var to a path of your choosing.

**Step 1: Build Envoy**

Using ``envoyproxy/envoy-build`` you will compile Envoy.
This image has all software needed to build Envoy. From your Envoy directory:

.. code-block:: console

  $ pwd
  src/envoy
  $ ./ci/run_envoy_docker.sh './ci/do_ci.sh release'

That command will take some time to run because it is compiling an Envoy binary and running tests.

If your system has limited resources, or you wish to build without running the tests, you can
also build as follows:

.. code-block:: console

  $ pwd
  src/envoy
  $ ./ci/run_envoy_docker.sh './ci/do_ci.sh release.server_only'

For more information on building and different build targets, please refer to :repo:`ci/README.md`.

.. warning::
   These instructions for building Envoy use
   `envoyproxy/envoy-build-ubuntu <https://hub.docker.com/r/envoyproxy/envoy-build-ubuntu/tags>`_ image.
   You will need 4-5GB of disk space to accommodate this image.

   This script runs as effective root on your host system.

**Step 2: Build image with only Envoy binary**

In this step we'll build the Envoy deployment images.

.. note::
   The ``docker`` CI target expects a release tarball to have been built previously using one of the steps above.

In order to build Docker inside the Envoy build image we need to set the env var ``ENVOY_DOCKER_IN_DOCKER``

.. code-block:: console

  $ pwd
  src/envoy/
  $ ENVOY_DOCKER_IN_DOCKER=1 ./ci/run_envoy_docker.sh './ci/do_ci.sh docker'

Now you can use the Envoy image to build the any of the sandboxes by changing
the ``FROM`` line in a related Dockerfile.

This can be particularly useful if you are interested in modifying Envoy, and testing
your changes.
