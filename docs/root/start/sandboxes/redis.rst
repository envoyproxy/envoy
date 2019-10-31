.. _install_sandboxes_redis_filter:

Redis Filter
============

In this example, we show how a :ref:`Redis filter <config_network_filters_redis_proxy>` can be used with the Envoy proxy. The Envoy proxy configuration includes a Redis filter that routes egress requests to redis server.

Running the Sandboxes
~~~~~~~~~~~~~~~~~~~~~

The following documentation runs through the setup of both services.

**Step 1: Install Docker**

Ensure that you have a recent versions of ``docker`` and ``docker-compose``.

A simple way to achieve this is via the `Docker Desktop <https://www.docker.com/products/docker-desktop>`_.

**Step 2: Clone the Envoy repo and start all of our containers**

If you have not cloned the Envoy repo, clone it with ``git clone git@github.com:envoyproxy/envoy``
or ``git clone https://github.com/envoyproxy/envoy.git``

Terminal 1

.. code-block:: console

  $ pwd
  envoy/examples/redis
  $ docker-compose pull
  $ docker-compose up --build -d
  $ docker-compose ps

      Name                   Command               State                             Ports
  ------------------------------------------------------------------------------------------------------------------
  redis_proxy_1   /docker-entrypoint.sh /bin       Up      10000/tcp, 0.0.0.0:1999->1999/tcp, 0.0.0.0:8001->8001/tcp
  redis_redis_1   docker-entrypoint.sh redis       Up      0.0.0.0:6379->6379/tcp

**Step 3: Issue Redis commands**

Issue Redis commands using your favourite Redis client, such as ``redis-cli``, and verify they are routed via Envoy.

Terminal 1

.. code-block:: console

  $ redis-cli -h localhost -p 1999 set foo foo
  OK
  $ redis-cli -h localhost -p 1999 set bar bar
  OK
  $ redis-cli -h localhost -p 1999 get foo
  "foo"
  $ redis-cli -h localhost -p 1999 get bar
  "bar"

**Step 4: Verify egress stats**

Go to ``http://localhost:8001/stats?usedonly&filter=redis.egress_redis.command`` and verify the following stats:

.. code-block:: none

  redis.egress_redis.command.get.total: 2
  redis.egress_redis.command.set.total: 2