.. _install_sandboxes_fault_injection:

Fault Injection Filter
======================

This simple example demonstrates Envoy's :ref:`fault injection <config_http_filters_fault_injection>` capability using Envoy's :ref:`runtime support <config_runtime>` to control the feature.

Running the Sandboxes
~~~~~~~~~~~~~~~~~~~~~

The following documentation runs through the setup of the services.

**Step 1: Install Docker**

Ensure that you have a recent versions of ``docker`` and ``docker-compose``.

A simple way to achieve this is via the `Docker Desktop <https://www.docker.com/products/docker-desktop>`_.

**Step 2: Clone the Envoy repo**

If you have not cloned the Envoy repo, clone it with:

``git clone git@github.com:envoyproxy/envoy``

or

``git clone https://github.com/envoyproxy/envoy.git``

**Step 3: Start all of our containers**

Terminal 1

.. code-block:: console

  $ pwd
  envoy/examples/fault-injection
  $ docker-compose pull
  $ docker-compose up --build -d
  $ docker-compose ps

            Name                         Command               State                             Ports
  ------------------------------------------------------------------------------------------------------------------------------
  fault-injection_backend_1   gunicorn -b 0.0.0.0:80 htt       Up      0.0.0.0:8080->80/tcp
  fault-injection_envoy_1     /docker-entrypoint.sh /usr       Up      10000/tcp, 0.0.0.0:9211->9211/tcp, 0.0.0.0:9901->9901/tcp

**Step 4: Start sending continuous stream of HTTP requests**

Terminal 2

.. code-block:: console

  $ pwd
  envoy/examples/fault-injection
  $ docker-compose exec envoy bash
  $ bash send_request.sh

The script above (``send_request.sh``) sends a continuous stream of HTTP requests to Envoy, which in turn forwards the requests to the backend container. Fauilt injection is configured in Envoy but turned off (i.e. affects 0% of requests). Consequently, you should see a continuous sequence of HTTP 200 response codes.

**Step 5: Test Envoy's abort fault injection**

Turn on *abort* fault injection via the runtime using the commands below.

Terminal 3

.. code-block:: console

  $ docker-compose exec envoy bash
  $ bash enable_abort_fault_injection.sh

The script above enables HTTP aborts for 100% of requests. So, you should now see a continuous sequence of HTTP 503
responses for all requests.

To disable the abort injection:

Terminal 3

.. code-block:: console

  $ bash disable_abort_fault_injection.sh

**Step 6: Test Envoy's delay fault injection**

Turn on *delay* fault injection via the runtime using the commands below.

Terminal 3

.. code-block:: console

  $ docker-compose exec envoy bash
  $ bash enable_delay_fault_injection.sh

The script above will add a 3-second delay to 50% of HTTP requests. You should now see a continuous sequence of HTTP 200 responses for all requests, but half of the requests will take 3 seconds to complete.

To disable the delay injection:

Terminal 3

.. code-block:: console

  $ bash disable_delay_fault_injection.sh

**Step 7: Check the current runtime filesystem**

To see the current runtime filesystem overview:

Terminal 3

.. code-block:: console

  $ tree /srv/runtime
