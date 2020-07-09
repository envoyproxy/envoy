.. _start:

Getting Started
===============

This section gets you started with a very simple configuration and provides some example configurations.

The fastest way to get started using Envoy is :ref:`installing pre-built binaries <install_binaries>`.
You can also :ref:`build it <building>` from source.

These examples use the :ref:`v3 Envoy API <envoy_api_reference>`, but use only the static configuration
feature of the API, which is most useful for simple requirements. For more complex requirements
:ref:`Dynamic Configuration <arch_overview_dynamic_config>` is supported.

Quick Start to Run Simple Example
---------------------------------

These instructions run from files in the Envoy repo. The sections below give a
more detailed explanation of the configuration file and execution steps for
the same configuration.

A very minimal Envoy configuration that can be used to validate basic plain HTTP
proxying is available in :repo:`configs/google_com_proxy.v2.yaml`. This is not
intended to represent a realistic Envoy deployment:

.. substitution-code-block:: none

  $ docker pull envoyproxy/|envoy_docker_image|
  $ docker run --rm -d -p 10000:10000 envoyproxy/|envoy_docker_image|
  $ curl -v localhost:10000

The Docker image used will contain the latest version of Envoy
and a basic Envoy configuration. This basic configuration tells
Envoy to route incoming requests to \*.google.com.

Simple Configuration
--------------------

Envoy can be configured using a single YAML file passed in as an argument on the command line.

The :ref:`admin message <envoy_v3_api_msg_config.bootstrap.v3.Admin>` is required to configure
the administration server. The `address` key specifies the
listening :ref:`address <envoy_v3_api_file_envoy/config/core/v3/address.proto>`
which in this case is simply `0.0.0.0:9901`.

.. code-block:: yaml

  admin:
    access_log_path: /tmp/admin_access.log
    address:
      socket_address: { address: 0.0.0.0, port_value: 9901 }

The :ref:`static_resources <envoy_v3_api_field_config.bootstrap.v3.Bootstrap.static_resources>` contains everything that is configured statically when Envoy starts,
as opposed to the means of configuring resources dynamically when Envoy is running.
The :ref:`v2 API Overview <config_overview>` describes this.

.. code-block:: yaml

    static_resources:

The specification of the :ref:`listeners <envoy_v3_api_file_envoy/config/listener/v3/listener.proto>`.

.. code-block:: yaml

      listeners:
      - name: listener_0
        address:
          socket_address: { address: 0.0.0.0, port_value: 10000 }
        filter_chains:
        - filters:
          - name: envoy.filters.network.http_connection_manager
            typed_config:
              "@type": type.googleapis.com/envoy.extensions.filters.network.http_connection_manager.v3.HttpConnectionManager
              stat_prefix: ingress_http
              codec_type: AUTO
              route_config:
                name: local_route
                virtual_hosts:
                - name: local_service
                  domains: ["*"]
                  routes:
                  - match: { prefix: "/" }
                    route: { host_rewrite_literal: www.google.com, cluster: service_google }
              http_filters:
              - name: envoy.filters.http.router

The specification of the :ref:`clusters <envoy_v3_api_file_envoy/service/cluster/v3/cds.proto>`.

.. code-block:: yaml

      clusters:
      - name: service_google
        connect_timeout: 0.25s
        type: LOGICAL_DNS
        # Comment out the following line to test on v6 networks
        dns_lookup_family: V4_ONLY
        lb_policy: ROUND_ROBIN
        load_assignment:
          cluster_name: service_google
          endpoints:
          - lb_endpoints:
            - endpoint:
                address:
                  socket_address:
                    address: www.google.com
                    port_value: 443
        transport_socket:
          name: envoy.transport_sockets.tls
          typed_config:
            "@type": type.googleapis.com/envoy.extensions.transport_sockets.tls.v3.UpstreamTlsContext
            sni: www.google.com


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
environment variables. This can be done, for example, on the Docker command line:

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


Sandboxes
---------

We've created a number of sandboxes using Docker Compose that set up different
environments to test out Envoy's features and show sample configurations. As we
gauge peoples' interests we will add more sandboxes demonstrating different
features. The following sandboxes are available:

.. toctree::
    :maxdepth: 2

    sandboxes/cors
    sandboxes/csrf
    sandboxes/ext_authz
    sandboxes/fault_injection
    sandboxes/front_proxy
    sandboxes/grpc_bridge
    sandboxes/jaeger_native_tracing
    sandboxes/jaeger_tracing
    sandboxes/load_reporting_service
    sandboxes/lua
    sandboxes/mysql
    sandboxes/redis
    sandboxes/zipkin_tracing

Other use cases
---------------

In addition to the proxy itself, Envoy is also bundled as part of several open
source distributions that target specific use cases.

.. toctree::
    :maxdepth: 2

    distro/ambassador
    distro/gloo
