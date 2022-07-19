.. _config_connection_balance_random:

Random Connection Balancer
=======================

* :ref:`v3 API reference <envoy_v3_api_msg_extensions.network.connection_balance.random.v3alpha.Random>`


This connection balancer extension balances connections randomly, which is a replacement of Exact connection balancer to ensure all threads work on Windows.

The Random connection balancer is only included in :ref:`contrib images <install_contrib>`

Example configuration
---------------------

An example for Dlb connection balancer configuration is:

.. literalinclude:: _include/dlb.yaml
    :language: yaml


How it works
------------

When new connections come, one worker thread will accept it and send it to one random worker.
