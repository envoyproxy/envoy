.. _config_qatzip:

Qatzip Compressor
=======================

* :ref:`v3 API reference <envoy_v3_api_msg_extensions.compression.qatzip.compressor.v3alpha.Qatzip>`


Qatzip compressor provides Envoy with faster hardware-accelerated gzip compression by integrating with `Intel® QuickAssist Technology (Intel® QAT) <https://www.intel.com/content/www/us/en/architecture-and-technology/intel-quick-assist-technology-overview.html>`_ through the qatlib and QATzip libraries.

Example configuration
---------------------

An example for Qatzip compressor configuration is:

.. literalinclude:: _include/qatzip.yaml
    :language: yaml


How it works
------------

If enabled, the Qatzip compressor will:

- attach Qat hardware
- create Threadlocal Qat session context for each worker thread

When a new http request comes, one worker thread will process it using its Qat session context and send the data needed to be compressed to
Qat hardware.

Installing and using QATzip
---------------------------

For information on how to build/install and use QATzip see `QATzip <https://github.com/intel/QATzip>`_.
