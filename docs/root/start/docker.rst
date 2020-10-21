.. _start_docker:

Using the Envoy Docker Image
============================

The following examples use the :ref:`official Envoy Docker image <start_install_docker>`.

These instructions are known to work for the ``x86_64`` and ``arm64`` architectures.

Running Envoy with docker-compose
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

If you would like to use Envoy with docker-compose you can overwrite the provided configuration file
by using a volume.

.. substitution-code-block:: yaml

  version: '3'
  services:
    envoy:
      image: envoyproxy/|envoy_docker_image|
      ports:
        - "10000:10000"
      volumes:
        - ./envoy.yaml:/etc/envoy/envoy.yaml

If you use this method, you will have to ensure that the ``envoy`` user can read the mounted file
either by ensuring the correct permissions on the file, or making it world-readable, as described
below.


Build and run a Docker image
~~~~~~~~~~~~~~~~~~~~~~~~~~~~

Create a simple Dockerfile to execute Envoy.

If you create a custom ``envoy.yaml`` you can create your own Docker image with it using the following
Dockerfile recipe:

.. substitution-code-block:: dockerfile

  FROM envoyproxy/|envoy_docker_image|
  COPY envoy.yaml /etc/envoy/envoy.yaml
  RUN chmod go+r /etc/envoy/envoy.yaml

Build the Docker image using:

.. code-block:: console

   $ docker build -t envoy:v1 .

Assuming Envoy is configured to listen on ports ``9901`` and ``10000``, you can now start it
with:

.. code-block:: console

   $ docker run -d --name envoy -p 9901:9901 -p 10000:10000 envoy:v1

Permissions for running Docker Envoy containers as a non-root user
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

The Envoy Docker image should be started as ``root``, but switches when run to the ``envoy`` user
created at build time.

The user is switched in the Docker ``ENTRYPOINT``.

Changing the ``uid`` and/or ``gid`` of the ``envoy`` user
*********************************************************

The default ``uid`` and ``gid`` for the ``envoy`` user are ``101``.

The ``uid`` and ``gid`` of this user can be set at runtime using the ``ENVOY_UID`` and ``ENVOY_GID``
environment variables.

This can be done, for example, on the Docker command line:

.. substitution-code-block:: console

  $ docker run -d --name envoy -e ENVOY_UID=777 -e ENVOY_GID=777 envoyproxy/|envoy_docker_image|

This can be useful if you wish to restrict or provide access to ``unix`` sockets inside the container, or
for controlling access to an Envoy socket from outside of the container.

To run the process inside  the container as the ``root`` user you can set ``ENVOY_UID`` to ``0``,
but doing so has the potential to weaken the security of your running container.

Logging permissions inside the Envoy container
**********************************************

The ``envoy`` image sends application logs to ``/dev/stdout`` and ``/dev/stderr`` by default, and these
can be viewed in the container log.

If you send application, admin or access logs to a file output, the ``envoy`` user will require the
necessary permissions to write to this file. This can be achieved by setting the ``ENVOY_UID`` and/or
by making the file writeable by the envoy user.

For example, to mount a log folder from the host and make it writable, you can:

.. substitution-code-block:: console

  $ mkdir logs
  $ chown 777 logs
  $ docker run -d --name envoy -v $(pwd)/logs:/var/log -e ENVOY_UID=777 envoyproxy/|envoy_docker_image|

You can then configure ``envoy`` to log to files in ``/var/log``

Configuration and binary file permissions inside the Envoy container
********************************************************************

The ``envoy`` user also needs to have permission to access any required configuration files mounted
into the container.

Any binary files specified in the configuration should also be executable by the ``envoy`` user.

If you are running in an environment with a strict ``umask`` setting, you may need to provide ``envoy``
with access by setting the ownership and/or permissions of the file.

One method of doing this without changing any file permissions is to start the container with the
host user's ``uid``, for example:

.. substitution-code-block:: console

  $ docker run -d --name envoy -v $(pwd)/envoy.yaml:/etc/envoy/envoy.yaml -e ENVOY_UID=$(id -u) envoyproxy/|envoy_docker_image|

Listen only on ports > 1024 inside the Docker Envoy container
*************************************************************

Unix-based systems restrict opening ``well-known`` ports (ie. with a port number < ``1024``) to the ``root`` user.

If you need to listen on a ``well-known`` port you can use Docker to do so.

For example, to create an Envoy server listening on port ``8000``, with forwarding from port ``80``:

.. substitution-code-block:: console

  $ docker run -d --name envoy -p 80:8000 envoyproxy/|envoy_docker_image|
