.. _install_sandboxes_templating:

Templating
==========

In this example, we show how to template Envoy's YAML file using environment variables.

Running the Sandboxes
~~~~~~~~~~~~~~~~~~~~~

The following documentation runs through the setup of this configuration.

**Step 1: Install Docker**

Ensure that you have a recent versions of ``docker`` and ``docker-compose``.

A simple way to achieve this is via the `Docker Desktop <https://www.docker.com/products/docker-desktop>`_.

**Step 2: Clone the Envoy repo and start the "dev" container**

If you have not cloned the Envoy repo, clone it with ``git clone git@github.com:envoyproxy/envoy``
or ``git clone https://github.com/envoyproxy/envoy.git``

Terminal 1

.. code-block:: console

  $ pwd
  envoy/examples/templating
  $ docker-compose pull
  $ docker-compose -f docker-compose-dev.yaml up --build -d
  $ docker-compose -f docker-compose-dev.yaml ps

            Name                          Command              State                            Ports
  ----------------------------------------------------------------------------------------------------------------------------
  templating_front-envoy_1        /docker-entrypoint.sh /bin ... Up      10000/tcp, 0.0.0.0:8000->80/tcp, 0.0.0.0:8001->8001/tcp

**Step 3: Send a request to get context specific "robots.txt" file**

The output from the ``curl`` command will show contents of the "robots-dev.txt" file.

Terminal 1

.. code-block:: console

  $ curl -v localhost:8000/robots.txt

  *   Trying ::1...
  * TCP_NODELAY set
  * Connected to localhost (::1) port 8000 (#0)
  > GET /robots.txt HTTP/1.1
  > Host: localhost:8000
  > User-Agent: curl/7.54.0
  > Accept: */*
  >
  < HTTP/1.1 200 OK
  < content-length: 45
  < content-type: text/plain
  < date: Thu, 19 Dec 2019 01:13:29 GMT
  < server: envoy
  <
  # Dev Environment

  User-agent: *
  Disallow: /
  * Connection #0 to host localhost left intact

**Step 4: Shutdown the "dev" container and start the "prod" container**

Terminal 1

.. code-block:: console

  $ pwd
  envoy/examples/templating
  $ docker-compose -f docker-compose-dev.yaml down
  $ docker-compose -f docker-compose-prod.yaml up --build -d
  $ docker-compose -f docker-compose-prod.yaml ps

            Name                          Command              State                            Ports
  ----------------------------------------------------------------------------------------------------------------------------
  templating_front-envoy_1        /docker-entrypoint.sh /bin ... Up      10000/tcp, 0.0.0.0:8000->80/tcp, 0.0.0.0:8001->8001/tcp

**Step 5: Send a request to get context specific "robots.txt" file**

The output from the ``curl`` command will show contents of the "robots-prod.txt" file.

Terminal 1

.. code-block:: console

  $ curl -v localhost:8000/robots.txt

  *   Trying ::1...
  * TCP_NODELAY set
  * Connected to localhost (::1) port 8000 (#0)
  > GET /robots.txt HTTP/1.1
  > Host: localhost:8000
  > User-Agent: curl/7.54.0
  > Accept: */*
  >
  < HTTP/1.1 200 OK
  < content-length: 43
  < content-type: text/plain
  < date: Thu, 19 Dec 2019 01:24:58 GMT
  < server: envoy
  <
  # Prod Environment

  User-agent: *
  Disallow
  * Connection #0 to host localhost left intact
