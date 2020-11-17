.. _arch_overview_compression_libraries:

压缩库
=======

底层实现
---------


目前 Envoy 在使用 `zlib <http://zlib.net>`_ 作为压缩库。

.. note::

  `zlib-ng <https://github.com/zlib-ng/zlib-ng>`_ 是一个存在多个包含最新优化的第三方贡献 Fork 库。
  这其中的很多优化对于 `提高压缩性能 <https://github.com/envoyproxy/envoy/issues/8448#issuecomment-667152013>`_ 有很大的用处。
  比如 Envoy 可以通过使用 --define zlib=ngbazel 选项，来构建 `zlib-ng <https://github.com/zlib-ng/zlib-ng>`_ 代替之前的 `zlib <http://zlib.net>`_ 。
  用于构建 `zlib-ng <https://github.com/zlib-ng/zlib-ng>`_ 的相关参数选项，可以在 :repo:`这里 <bazel/foreign_cc/BUILD>` 找到。
  但是，这些参数选项目前只能在 Linux 上使用。 
