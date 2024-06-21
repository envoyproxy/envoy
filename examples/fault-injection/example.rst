.. _install_sandboxes_fault_injection:

Fault injection filter
======================

.. sidebar:: Requirements

   .. include:: _include/docker-env-setup-link.rst

This simple example demonstrates Envoy's :ref:`fault injection <config_http_filters_fault_injection>` capability
using Envoy's :ref:`runtime support <config_runtime>` to control the feature.

It demonstrates fault injection that cause the request to abort and fail, and also faults that simply delay the
response.

Step 1: Start all of our containers
***********************************

Change to the ``examples/fault_injection`` directory.

Terminal 1

.. code-block:: console

  $ pwd
  envoy/examples/fault-injection
  $ docker compose pull
  $ docker compose up --build -d
  $ docker compose ps

            Name                         Command               State               Ports
  ------------------------------------------------------------------------------------------------------
  fault-injection_backend_1   gunicorn -b 0.0.0.0:80 htt       Up      0.0.0.0:8080->80/tcp
  fault-injection_envoy_1     /docker-entrypoint.sh /usr       Up      10000/tcp, 0.0.0.0:9211->9211/tcp

Step 2: Start sending continuous stream of HTTP requests
********************************************************

Terminal 2

.. code-block:: console

  $ pwd
  envoy/examples/fault-injection
  $ docker compose exec envoy bash
  $ bash send_request.sh

The script above (:download:`send_request.sh <_include/fault-injection/send_request.sh>`) sends a continuous stream
of HTTP requests to Envoy, which in turn forwards the requests to the backend container.

Fault injection is configured in Envoy but turned off (i.e. affects 0% of requests).

Consequently, you should see a continuous sequence of ``HTTP 200`` response codes.

Step 3: Test Envoy's abort fault injection
******************************************

Turn on *abort* fault injection via the runtime using the commands below.

Terminal 3

.. code-block:: console

  $ docker compose exec envoy bash
  $ bash enable_abort_fault_injection.sh

The script above enables ``HTTP`` aborts for 100% of requests.

You should now see a continuous sequence of ``HTTP 503`` responses for all requests.

To disable the abort injection:

Terminal 3

.. code-block:: console

  $ bash disable_abort_fault_injection.sh

Step 4: Test Envoy's delay fault injection
******************************************

Turn on *delay* fault injection via the runtime using the commands below.

Terminal 3

.. code-block:: console

  $ docker compose exec envoy bash
  $ bash enable_delay_fault_injection.sh

The script above will add a 3-second delay to 50% of ``HTTP`` requests.

You should now see a continuous sequence of ``HTTP 200`` responses for all requests, but half of the requests
will take 3 seconds to complete.

To disable the delay injection:

Terminal 3

.. code-block:: console

  $ bash disable_delay_fault_injection.sh

Step 5: Check the current runtime filesystem
********************************************

To see the current runtime filesystem overview:

Terminal 3

.. code-block:: console

  $ tree /srv/runtime

.. seealso::

   :ref:`Fault injection <config_http_filters_fault_injection>`
      Learn more about Envoy's ``HTTP`` fault injection filter.
