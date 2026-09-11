.. _arch_overview_compression_libraries:

Compression Libraries
=====================

Underlying implementation
-------------------------

Currently Envoy uses `zlib-ng <https://github.com/zlib-ng/zlib-ng>`_, `brotli <https://brotli.org>`_ and
`zstd <https://facebook.github.io/zstd>`_ as compression libraries.

.. note::

  `zlib-ng <https://github.com/zlib-ng/zlib-ng>`_ is a fork that hosts several 3rd-party
  contributions containing new optimizations. Those optimizations are considered useful for
  `improving compression performance <https://github.com/envoyproxy/envoy/issues/8448#issuecomment-667152013>`_.
  Envoy is built using `zlib-ng <https://github.com/zlib-ng/zlib-ng>`_, you can link an alternative implementation
  using e.g. `--@envoy//bazel:zlib=@zlib`. This would require registering the zlib repository with Bazel.
  Bazel option. The relevant build options used to build `zlib-ng <https://github.com/zlib-ng/zlib-ng>`_ can be
  evaluated in :repo:`here <bazel/external/zlib_ng.BUILD>`.
