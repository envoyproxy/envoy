.. _config_http_filters_decompressor:

解压缩器
============
解压缩器是一个 HTTP 过滤器，它可以使 Envoy 支持双向解压缩数据。


配置
-------------
* :ref:`v3 API reference <envoy_v3_api_msg_extensions.filters.http.decompressor.v3.Decompressor>`

它是如何工作的
---------------
当启用解压缩过滤器时，会检查头部，以确定是否应该对内容进行解压缩。这个内容将会被解压缩，并继续传输到剩余的其他过滤链。需要注意的是，解压缩会根据以下描述的规则分别对请求和响应进行独立工作。

当前过滤器只支持 :ref:`gzip 压缩 <envoy_v3_api_msg_extensions.compression.gzip.decompressor.v3.Gzip>`
。可以支持其他压缩库作为扩展。

过滤器的示例配置如下所示：

.. code-block:: yaml

    http_filters:
    - name: decompressor
      typed_config:
        "@type": type.googleapis.com/envoy.extensions.filters.http.decompressor.v3.Decompressor
        decompressor_library:
          name: basic
          typed_config:
            "@type": type.googleapis.com/envoy.extensions.compression.gzip.decompressor.v3.Gzip
            window_bits: 10

*默认情况* 下当发生如下情况，解压会 *被跳过* ：

- 请求/响应头部不包含 *content-encoding* 。
- 请求/响应头部包含 *content-encoding* ，但是不包含配置的解压缩器的内容编码。
- 请求/响应头部包含 *cache-control* ，该头的值包含 "no-transform"。

当解压缩被应用的时候，会发生如下情况：

- *content-length* 会被从头部移除。

  .. note::

    如果需要更新头部的内容长度 *content-length* ，则可以将缓冲过滤器作为过滤链的一部分，以缓冲解压缩的帧，并最终更新头部。由于过滤器排序
    :ref:`filter ordering <arch_overview_http_filters_ordering>` ，缓冲过滤器需要在请求中安装在解压缩过滤器之后，以及在响应中安装在解压器过滤器之前。

- 头部中内容编码 *content-encoding* 会被修改以删除已应用的解压缩。

- *x-envoy-decompressor-<decompressor_name>-<compressed/uncompressed>-bytes* 尾部会被添加到请求/响应中以传达有关解压缩的信息。

.. _decompressor-statistics:

对请求和响应使用不同的解压缩器
--------------------------------------------------------

如果请求和响应需要不同的压缩库，则可以安装不同的解压缩过滤器仅针对请求或者响应启用。例如：

.. code-block:: yaml

  http_filters:
  # This filter is only enabled for requests.
  - name: envoy.filters.http.decompressor
    typed_config:
      "@type": type.googleapis.com/envoy.extensions.filters.http.decompressor.v3.Decompressor
      decompressor_library:
        name: small
        typed_config:
          "@type": "type.googleapis.com/envoy.extensions.compression.gzip.decompressor.v3.Gzip"
          window_bits: 9
          chunk_size: 8192
      response_direction_config:
        common_config:
          enabled:
            default_value: false
            runtime_key: response_decompressor_enabled
  # This filter is only enabled for responses.
  - name: envoy.filters.http.decompressor
    typed_config:
      "@type": type.googleapis.com/envoy.extensions.filters.http.decompressor.v3.Decompressor
      decompressor_library:
        name: large
        typed_config:
          "@type": "type.googleapis.com/envoy.extensions.compression.gzip.decompressor.v3.Gzip"
          window_bits: 12
          chunk_size: 16384
      request_direction_config:
        common_config:
          enabled:
            default_value: false
            runtime_key: request_decompressor_enabled

统计
----------

每一个配置的解压缩过滤器都有一个以<stat_prefix>.decompressor.<decompressor_library.name>.<decompressor_library_stat_prefix>.<request/response>* 为根的统计，具有以下内容：

.. csv-table::
  :header: 名称, 类型, 描述
  :widths: 1, 1, 2

  decompressed, Counter, 压缩的请求/响应数。
  not_decompressed, Counter, 未压缩的请求/响应数。
  total_uncompressed_bytes, Counter, 所有标记为解压缩的请求/响应的未压缩字节总数。
  total_compressed_bytes, Counter, 标记为解压缩的所有请求/响应的总压缩字节。

解压缩库的其他统计参考以根为 <stat_prefix>.decompressor.<decompressor_library.name>.<decompressor_library_stat_prefix>.decompressor_library 的统计。
