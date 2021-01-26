.. _config_http_filters_compressor:

压缩器
==========
压缩器是一个 HTTP 过滤器，它允许 Envoy 基于客户端请求将从上游服务发过来的数据进行压缩。
当带宽有限或者负载（payload）很大时，压缩是非常有用的，只不过代价是更高的 CPU 负载或者
把压缩卸载到一个压缩加速器。

.. note::

 这个过滤器是为了替换 :ref:`HTTP Gzip 过滤器 <config_http_filters_gzip>`

配置说明
-------------
* :ref:`v3 API 参考 <envoy_v3_api_msg_extensions.filters.http.compressor.v3.Compressor>`
* 这个过滤器配置的名称为 *envoy.filters.http.compressor*

工作原理
------------
当过滤器启用后，会根据请求和应答报文头判断是否需要压缩报文体。
如果请求与应答允许，则正文将被压缩，然后使用适当的报文头将其发送到客户端。

当前过滤器只支持 :ref:`gzip compression <envoy_v3_api_msg_extensions.compression.gzip.compressor.v3.Gzip>`
其它压缩库可作为扩展来支持。

配置示例如下：

.. code-block:: yaml

    http_filters:
    - name: envoy.filters.http.compressor
      typed_config:
        "@type": type.googleapis.com/envoy.extensions.filters.http.compressor.v3.Compressor
        disable_on_etag_header: true
        content_length: 100
        content_type:
          - text/html
          - application/json
        compressor_library:
          name: text_optimized
          typed_config:
            "@type": type.googleapis.com/envoy.extensions.compression.gzip.compressor.v3.Gzip
            memory_level: 3
            window_bits: 10
            compression_level: best_compression
            compression_strategy: default_strategy

这些情况下，压缩器将不启用：

- 请求头中不包含 *accept-encoding*
- 请求头中虽然包含 *accept-encoding* ，但取值中不包含“gzip”或者“\*”
- 请求头中 *accept-encoding* 取值包含“gzip” or “\*”但权重配置“q=0”
  需要注意的是：“gzip”优先级比“\*”高。例如，当 *accept-encoding*
  取值为“gzip;q=0,\*;q=1”，过滤器将不会压缩。但如果它取值为
  “\*;q=0,gzip;q=1”，过滤器将进行压缩
- 请求头中 *accept-encoding* 中包含其它比“gzip”权重更高的压缩算法，
  且该算法过滤器在处理链中存在。
- 应答报文头中包含 *content-encoding*
- 应答报文头中 *cache-control* 取值包含“no-transform”
- 应答报文头中 *transfer-encoding* 取值包含“gzip”
- 应答报文头中 *content-type* 取值不在这些当中：*application/javascript*, *application/json*,
  *application/xhtml+xml*, *image/svg+xml*, *text/css*, *text/html*, *text/plain*,
  *text/xml*
- 应答报文头中不包含 *content-length* 和 *transfer-encoding*
- 应答报文体小于 30 字节 (仅适用于 *transfer-encoding* 取值不为：chunked)

需要注意的是，如果过滤器配置的是 gzip 之外的扩展压缩库，将从扩展提供的报文头 *accept-encoding* 中获取
编码方式。


当压缩器生效，将有如下影响：

- 应答报文头中 *content-length* 字段将被去除
- 应答报文头中包含“*transfer-encoding: chunked*”且不包含“*content-encoding*”。
- 每个应答报文头中都会带上“*vary: accept-encoding*”

同时，即使由于不兼容的 “accept-encoding” 头部，压缩没有被启用，"*vary: accept-encoding*" 头部也可以被插入。
当给定兼容的 "*accept-encoding*" 仍可以被压缩时，这种情况就会发生。否则，如果未压缩的响应被 Envoy
前面的缓存代理缓存，那么该代理将不知道如何用兼容的 "*accept-encoding*" 从上游获取新传入的请求。

.. _compressor-statistics:

统计信息
----------

所有配置压缩过滤器的指标前缀为
<stat_prefix>.compressor.<compressor_library.name>.<compressor_library_stat_prefix>.*
指标项如下:

.. csv-table::
  :header: 名称, 类型, 描述
  :widths: 1, 1, 2

  compressed, Counter, 已压缩的请求数
  not_compressed, Counter, 未压缩的请求数
  no_accept_header, Counter, 请求头不匹配的请求数
  header_identity, Counter, 请求头中包含 *accept-encoding* 的请求数
  header_compressor_used, Counter, 请求头中 *accept-encoding* 设置为“gzip”的请求数
  header_compressor_overshadowed, Counter, 由于已经匹配链路中其它过滤器而跳过的请求数
  header_wildcard, Counter, 请求头中 *accept-encoding* 设置为“\*”的请求数
  header_not_valid, Counter, 请求头中 *accept-encoding* 取值不合法的请求数（比如“q=0”或者其它不支持的压缩方式）
  total_uncompressed_bytes, Counter, 标记为需要压缩但未进行压缩的字节总数
  total_compressed_bytes, Counter, 标记为需要压缩且已经压缩的字节总数
  content_length_too_small, Counter, 标记为需要压缩但由于报文字节数太小未进行压缩的请求数
  not_compressed_etag, Counter, 由于扩展报文头而未进行压缩的请求数。只有当开启 *disable_on_etag_header* 时才有该指标
