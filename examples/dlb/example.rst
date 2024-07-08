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

   The output that is not empty means CPU supports DLB. The number and PCIE address of DLB devices vary from CPU to CPU.

   The minimum support kernel version is 5.15.

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

 $ ./envoy --concurrency 2 -c dlb_example_config.yaml --log-level debug &> envoy-dlb.log

After Envoy starts, you should see logs similar to:

.. code-block:: console

 $ grep dlb envoy-dlb.log

.. code-block:: text

  [2024-07-08 10:05:00.113][3596312][info][main] [source/server/server.cc:434]   envoy.network.connection_balance: envoy.network.connection_balance.dlb
  [2024-07-08 10:05:00.241][3596312][debug][config] [contrib/dlb/source/connection_balancer_impl.cc:92] dlb available resources: domains: 32, LDB queues: 32, LDB ports: 64, ES entries: 2048, Contig ES entries: 2048, LDB credits: 8192, Config LDB credits: 8192, LDB credit pools: 64

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

Check the log, you should see the latest contents similar to:

.. code-block:: text

  [2024-07-08 10:10:48.099][3598062][debug][connection] [contrib/dlb/source/connection_balancer_impl.cc:283] worker_1 dlb send fd 49
  [2024-07-08 10:10:48.099][3598066][debug][connection] [contrib/dlb/source/connection_balancer_impl.cc:300] worker_3 get dlb event 1
  [2024-07-08 10:10:48.099][3598066][debug][connection] [contrib/dlb/source/connection_balancer_impl.cc:317] worker_3 dlb recv 49
  [2024-07-08 10:10:48.099][3598066][debug][connection] [contrib/dlb/source/connection_balancer_impl.cc:297] worker_3 dlb receive none, skip

Above logs show that DLB balanaces a connection from worker 1 to worker 3.

.. seealso::
   :ref:`DLB connection balanace API <envoy_v3_api_msg_extensions.network.connection_balance.dlb.v3alpha.Dlb>`
      API and configuration reference for Envoy's DLB connection balanace.

   :ref:`Connection balance configuration <envoy_v3_api_field_config.listener.v3.Listener.connection_balance_config>`
      Configuration referenc for Envoy's connection balanace.

  `DLB <https://networkbuilders.intel.com/solutionslibrary/queue-management-and-load-balancing-on-intel-architecture>`_
    The Intel DLB website.
