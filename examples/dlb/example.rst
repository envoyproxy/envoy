.. _install_sandboxes_dlb:

DLB
===

By enabling connection balancer in Envoy you can balance the connections across the threads and improve performance.

This sandbox provides an example about how to enable DLB connection balanace.

.. note::
   Please run below command to check your CPU supports DLB:

   .. code-block:: console

   $ lspci -d :2710

   You should get output like below:

   .. code-block:: text

      5a:00.0 Co-processor: Intel Corporation Device 2710
      6b:00.0 Co-processor: Intel Corporation Device 2710
      7c:00.0 Co-processor: Intel Corporation Device 2710
      8d:00.0 Co-processor: Intel Corporation Device 2710

   The number and PCIE address of DLB devices vary from CPU to CPU.

   Also check your kernel version, 5.15+ is good.

Step 1: Install DLB Driver
**************************

You can download the DLB driver release tarball from the `DLB website <https://www.intel.com/content/www/us/en/download/686372/intel-dynamic-load-balancer.html>`_.

To install it refer to `the getting started guide <https://downloadmirror.intel.com/727424/DLB_Driver_User_Guide.pdf>`_.

Step 2: Run Envoy with DLB connection balanace enabled
******************************************************

With the example configuration Envoy listens on port 10000 and proxies to an upstream server listening on port 12000.

.. literalinclude:: _include/dlb/dlb_example_config.yaml
 :language: yaml
 :lines: 7-11
 :lineno-start: 7
 :linenos:
 :caption: :download:`dlb_example_config.yaml <_include/dlb/dlb_example_config.yaml>`

.. code-block:: console

 $ ./envoy --concurrency 2 -c dlb_example_config.yaml

Step 3: Run the upstream service
********************************

.. code-block:: console

 $ docker run -d -p 12000:80 nginx

Step 3: Test
************

Visit the upstream service by Envoy endpoint:

.. code-block:: console

 $ curl localhost:10000 | grep Welcome

You should get output from Nginx:

.. code-block:: text

  <title>Welcome to nginx!</title>

.. seealso::
   :ref:`DLB connection balanace API <envoy_v3_api_msg_extensions.network.connection_balance.dlb.v3alpha.Dlb>`
      API and configuration reference for Envoy's DLB connection balanace.

   :ref:`Connection balance configuration <envoy_v3_api_field_config.listener.v3.Listener.connection_balance_config>`
      Configuration referenc for Envoy's connection balanace.

  `DLB <https://networkbuilders.intel.com/solutionslibrary/queue-management-and-load-balancing-on-intel-architecture>`_
    The Intel DLB website.
