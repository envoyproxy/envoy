.. _config_connection_balance_dlb:

Dlb Connection Balancer
=======================

* :ref:`v3 API reference <envoy_v3_api_msg_extensions.network.connection_balance.dlb.v3alpha.Dlb>`


This connection balancer extension provides Envoy with low latency networking by integrating with `Intel DLB <https://networkbuilders.intel.com/solutionslibrary/queue-management-and-load-balancing-on-intel-architecture>`_ through the libdlb library.

The Dlb connection balancer is only included in :ref:`contrib images <install_contrib>`

Example configuration
---------------------

An example for Dlb connection balancer configuration is:

.. literalinclude:: _include/dlb.yaml
    :language: yaml


How it works
------------

If enabled, the Dlb connection balancer will:

- attach Dlb hardware
- create a queue for balancing
- create one port to send and one port to receive for each worker thread
- create one eventfd for each worker thread and attach each eventfd to corresponding customer
- register each eventfd to corresponding customer and Dlb hardware

When new connections come, one worker thread will accept it and send it to Dlb hardware. Dlb hardware
does balancing then trigger one worker thread to receive via libevent.

Installing and using Dlb
------------------------

For information on how to build/install and use libdlb see `the getting started guide <https://downloadmirror.intel.com/727424/DLB_Driver_User_Guide.pdf>`_.
