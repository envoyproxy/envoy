.. _on_your_laptop:


On your laptop
==============

Before running Envoy in a production setting, you might want to tour its
capabilities. In this article, we'll walk through how to run Envoy on your
laptop, test proxy configurations, and observe results.

Requirements
~~~~~~~~~~~~

While you can :ref:`build Envoy from source <building>`
the easiest way to get started is by using the `official Docker images <https://hub.docker.com/u/envoyproxy/>`_.
So before starting out, you'll need the following software installed and configured:

- `Docker <https://docs.docker.com/install/>`_
- `Docker Compose <https://docs.docker.com/compose/install/>`_
- `Git <https://help.github.com/articles/set-up-git/>`_
- `curl <https://curl.haxx.se/>`_

We use Docker and Docker Compose to set up and run example service topologies
using Envoy, git to access the Envoy examples, and curl to send traffic to
running services.

Running Envoy
~~~~~~~~~~~~~

Running the latest Docker image will technically get you Envoy on your laptop,
but without a config file it won't do anything very interesting. Let's get a
simple front proxy topology running, which will send traffic to two service
backends. The `Envoy source repository <https://github.com/envoyproxy/envoy>`_
has a couple of examples, so to start, clone that repository and go to the
``examples/front-proxy`` directory. This contains Dockerfiles, config files and a
Docker Compose manifest for setting up a the topology.

.. code-block:: console

   $ git clone https://github.com/envoyproxy/envoy
   $ cd envoy/examples/front-proxy

The services run a very simple Flask application, defined in ``service.py``. An
Envoy runs in the same container as a sidecar, configured with the
``service-envoy.yaml`` file. Finally, the ``Dockerfile-service`` creates a container
that runs Envoy and the service on startup.

The front proxy is simpler. It runs Envoy, configured with the
``front-envoy.yaml`` file, and uses ``Dockerfile-frontenvoy`` as its container
definition.

The ``docker-compose.yaml`` file provides a description of how to build, package,
and run the front proxy and services together.

If using Windows, make sure in Docker Desktop > Settings > Shared Drives, the drive you're
using to run the ``front-proxy`` example is shared/checked. Otherwise, you'll get errors like:
``ERROR: for front-proxy_front-envoy_1  Cannot create container for service front-envoy: b'Drive has not been shared'``.

To build our containers, run:

.. code-block:: console

   $ docker-compose up --build -d

This starts a single instance of the front proxy and two service instances, one
configured as "service1" and the other as "service2", ``--build`` means build
containers before starting up, and ``-d`` means run them in detached mode.

Running ``docker-compose ps`` should show the following output:

.. code-block:: console

   $ docker-compose ps
           Name                        Command               State                      Ports
   ----------------------------------------------------------------------------------------------------------------
   frontproxy_front-envoy_1   /bin/sh -c /usr/local/bin/ ... Up      0.0.0.0:8000->80/tcp, 0.0.0.0:8001->8001/tcp
   frontproxy_service1_1      /bin/sh -c /usr/local/bin/ ... Up      80/tcp
   frontproxy_service2_1      /bin/sh -c /usr/local/bin/ ... Up      80/tcp

Sending Traffic
~~~~~~~~~~~~~~~

Docker Compose has mapped port 8000 on the front-proxy to your local
network. Open your browser to http://localhost:8000/service/1, or run ``curl
localhost:8000/service/1``. You should see

.. code-block:: console

   $ curl localhost:8000/service/1
   Hello from behind Envoy (service 1)! hostname: 6632a613837e resolvedhostname: 172.19.0.3

Going to http://localhost:8000/service/2 should result in

.. code-block:: console

   $ curl localhost:8000/service/2
   Hello from behind Envoy (service 2)! hostname: bf97b0b3294d resolvedhostname: 172.19.0.2

You're connecting to Envoy, operating as a front proxy, which is in turn sending
your request to service 1 or service 2.

Configuring Envoy
~~~~~~~~~~~~~~~~~

This is a simple way to configure Envoy statically for the purpose of
demonstration. As we move on, you'll see how you can really harness its power by
dynamically configuring it.

Let's take a look at how Envoy is configured. To get the right services set up,
Docker Compose looks at the ``docker-compose.yaml`` file. You'll see the following
definition for the ``front-envoy`` service:

.. code-block:: yaml

  front-envoy:
    build:
      context: ../
      dockerfile: front-proxy/Dockerfile-frontenvoy
    volumes:
      - ./front-envoy.yaml:/etc/front-envoy.yaml
    networks:
      - envoymesh
    expose:
      - "80"
      - "8001"
    ports:
      - "8000:80"
      - "8001:8001"

Going from top to bottom, this says:

  1. Build a container using the ``Dockerfile-frontenvoy`` file located in the
  current directory
  2. Mount the ``front-envoy.yaml`` file in this directory as ``/etc/front-envoy.yaml``
  3. Create and use a Docker network named "``envoymesh``" for this container
  4. Expose ports 80 (for general traffic) and 8001 (for the admin server)
  5. Map the host port 8000 to container port 80, and the host port 8001 to
  container port 8001

Knowing that our front proxy uses the ``front-envoy.yaml`` to configure Envoy,
let's take a deeper look. Our file has two top level elements,
``static_resources`` and ``admin``.

.. code-block:: yaml

   static_resources:
   admin:

The ``admin`` block is relatively simple.

.. code-block:: yaml

   admin:
     access_log_path: "/dev/null"
     address:
     socket_address:
       address: 0.0.0.0
       port_value: 8001

The ``access_log_path`` field is set to ``/dev/null``, meaning access logs to the
admin server are discarded. In a testing or production environment, users would
change this value to an appropriate destination. The ``address`` object tells
Envoy to create an admin server listening on port 8001.

The ``static_resources`` block contains definitions for clusters and listeners
that aren't dynamically managed. A cluster is a named group of hosts/ports, over
which Envoy will load balance traffic, and listeners are named network locations
that clients can connect to. The ``admin`` block configures our admin server.

Our front proxy has a single listener, configured to listen on port 80, with
a filter chain that configures Envoy to manage HTTP traffic.

.. code-block:: yaml

  listeners:
  - address:
      socket_address:
        address: 0.0.0.0
        port_value: 80
    filter_chains:
    - filters:
      - name: envoy.http_connection_manager
        config:
          codec_type: auto
          stat_prefix: ingress_http
          route_config:
            name: local_route

Within the configuration for our HTTP connection manager filter, there is a
definition for a single virtual host, configured to accept traffic for all
domains.

.. code-block:: yaml

            virtual_hosts:
            - name: backend
              domains:
              - "*"
              routes:
              - match:
                  prefix: "/service/1"
                route:
                  cluster: service1
              - match:
                  prefix: "/service/2"
                route:
                  cluster: service2

Routes are configured here, mapping traffic for ``/service/1`` and ``/service/2`` to
the appropriate clusters.

Next come static cluster definitions:

.. code-block:: yaml

  clusters:
  - name: service1
    connect_timeout: 0.25s
    type: strict_dns
    lb_policy: round_robin
    http2_protocol_options: {}
    hosts:
    - socket_address:
        address: service1
        port_value: 80
  - name: service2
    connect_timeout: 0.25s
    type: strict_dns
    lb_policy: round_robin
    http2_protocol_options: {}
    hosts:
    - socket_address:
        address: service2
        port_value: 80

You can configure timeouts, circuit breakers, discovery settings, and more on
clusters. Clusters are composed of endpoints – a set of network locations that
can serve requests for the cluster. In this example, endpoints are canonically
defined in DNS, which Envoy can read from. Endpoints can also be defined
directly as socket addresses, or read dynamically via
the Endpoint Discovery Service.

Modifying Configuration
~~~~~~~~~~~~~~~~~~~~~~~

In Envoy, you can modify the config files, rebuild Docker images, and test the
changes. Listener filters are Envoy's way of attaching additional functionality
to listeners. For instance, to add access logging to your HTTP filter, add the
``access_log`` object to your filter config, as shown here.


.. code-block:: yaml

    - filters:
      - name: envoy.http_connection_manager
        config:
          codec_type: auto
          stat_prefix: ingress_http
          access_log:
            - name: envoy.file_access_log
              config:
                path: "/var/log/access.log"
          route_config:


Destroy your Docker Compose stack with ``docker-compose down``, then rebuild it
with ``docker-compose up --build -d``. Make a few requests to your services using
curl, then log into a shell with ``docker-compose exec front-envoy /bin/bash``. An
``access.log`` file should be in ``/var/log``, showing the results of your requests.

Admin Server
~~~~~~~~~~~~

A great feature of Envoy is the built-in admin server. If you visit
``http://localhost:8001`` in your browser you should see a page with links to more
information. The ``/clusters`` endpoint shows statistics on upstream clusters, and
the ``stats`` endpoint shows more general statistics. You can get information
about the server build at ``/server_info``, query and alter logging levels at
``/logging``. General help is available at the ``/help`` endpoint.

Further Exploration
~~~~~~~~~~~~~~~~~~~

If you're interested in exploring more of Envoy's capabilities,
the :ref:`Envoy examples <start_sandboxes>` have more complex topologies that will
get you slightly more real-world, but still use statically discovered examples. If
you'd like to learn more about how to operate Envoy in a production setting, the
:ref:`service discovery integration <service_discovery>` walks through what it
means to integrate Envoy with your existing environment. If you run into issues
as you begin to test out Envoy, be sure to visit :ref:`getting help <getting_help>`
to learn where to report issues, and who to message.
