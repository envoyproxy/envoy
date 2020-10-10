.. _start_docker:

Using the Envoy Docker Image
----------------------------

Create a simple Dockerfile to execute Envoy, which assumes that envoy.yaml (described above) is in your local directory.
You can refer to the :ref:`Command line options <operations_cli>`.

.. substitution-code-block:: none

  FROM envoyproxy/|envoy_docker_image|
  COPY envoy.yaml /etc/envoy/envoy.yaml

Build the Docker image that runs your configuration using::

  $ docker build -t envoy:v1 .

And now you can execute it with::

  $ docker run -d --name envoy -p 9901:9901 -p 10000:10000 envoy:v1

And finally, test it using::

  $ curl -v localhost:10000

If you would like to use Envoy with docker-compose you can overwrite the provided configuration file
by using a volume.

.. substitution-code-block: yaml

  version: '3'
  services:
    envoy:
      image: envoyproxy/|envoy_docker_image|
      ports:
        - "10000:10000"
      volumes:
        - ./envoy.yaml:/etc/envoy/envoy.yaml

By default the Docker image will run as the ``envoy`` user created at build time.

The ``uid`` and ``gid`` of this user can be set at runtime using the ``ENVOY_UID`` and ``ENVOY_GID``
environment variables. This can be done, for example, on the Docker command line::

  $ docker run -d --name envoy -e ENVOY_UID=777 -e ENVOY_GID=777 -p 9901:9901 -p 10000:10000 envoy:v1

This can be useful if you wish to restrict or provide access to ``unix`` sockets inside the container, or
for controlling access to an ``envoy`` socket from outside of the container.

If you wish to run the container as the ``root`` user you can set ``ENVOY_UID`` to ``0``.

The ``envoy`` image sends application logs to ``/dev/stdout`` and ``/dev/stderr`` by default, and these
can be viewed in the container log.

If you send application, admin or access logs to a file output, the ``envoy`` user will require the
necessary permissions to write to this file. This can be achieved by setting the ``ENVOY_UID`` and/or
by making the file writeable by the envoy user.

For example, to mount a log folder from the host and make it writable, you can:

.. substitution-code-block:: none

  $ mkdir logs
  $ chown 777 logs
  $ docker run -d -v `pwd`/logs:/var/log --name envoy -e ENVOY_UID=777 -p 9901:9901 -p 10000:10000 envoy:v1

You can then configure ``envoy`` to log to files in ``/var/log``

The default ``envoy`` ``uid`` and ``gid`` are ``101``.

The ``envoy`` user also needs to have permission to access any required configuration files mounted
into the container.

If you are running in an environment with a strict ``umask`` setting, you may need to provide envoy with
access either by setting the ``uid`` or ``gid`` of the file, or by making the configuration file readable
by the envoy user.

One method of doing this without changing any file permissions or running as root inside the container
is to start the container with the host user's ``uid``, for example:

.. substitution-code-block:: none

  $ docker run -d --name envoy -e ENVOY_UID=`id -u` -p 9901:9901 -p 10000:10000 envoy:v1
