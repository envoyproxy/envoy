.. _arch_overview_compression_libraries:

Compression Libraries
=====================

Underlying implementation
-------------------------

Currently Envoy uses `zlib <http://zlib.net>`_ as a compression library.

.. note::

  `zlib-ng <https://github.com/zlib-ng/zlib-ng>`_ is a fork that hosts several 3rd-party
  contributions containing new optimizations. Those optimizations are considered useful for
  `improving compression performance <https://github.com/envoyproxy/envoy/issues/8448#issuecomment-667152013>`_.
  Envoy can be built to use `zlib-ng <https://github.com/zlib-ng/zlib-ng>`_ instead of regular
  `zlib <http://zlib.net>`_ by using ``--define zlib=ng`` Bazel option. The relevant build options
  used to build `zlib-ng <https://github.com/zlib-ng/zlib-ng>`_ can be evaluated in :repo:`here
  <bazel/foreign_cc/BUILD>`. Currently, this option is only available on Linux.
