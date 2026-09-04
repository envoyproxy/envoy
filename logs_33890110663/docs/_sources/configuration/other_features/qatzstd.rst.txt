.. _config_qatzstd:

Qatzstd Compressor
=======================

* :ref:`v3 API reference <envoy_v3_api_msg_extensions.compression.qatzstd.compressor.v3alpha.Qatzstd>`


Qatzstd compressor provides Envoy with faster hardware-accelerated zstd compression by integrating with `Intel® QuickAssist Technology (Intel® QAT) <https://www.intel.com/content/www/us/en/architecture-and-technology/intel-quick-assist-technology-overview.html>`_ through the qatlib and QAT-ZSTD-Plugin libraries.

Example configuration
---------------------

An example for Qatzstd compressor configuration is:

.. literalinclude:: _include/qatzstd.yaml
    :language: yaml


How it works
------------

If enabled, the Qatzstd compressor will:

- attach Qat hardware
- create Threadlocal Qatzstd context for each worker thread

When new http package come, one worker thread will process it using its Qatzstd context and send the data needed to be compressed to
Qat hardware using standard zstd api.

Installing and using QAT-ZSTD-Plugin
------------------------------------

For information on how to build/install and use QAT-ZSTD-Plugin see `introduction <https://github.com/intel/QAT-ZSTD-Plugin/tree/main#introduction>`_.
