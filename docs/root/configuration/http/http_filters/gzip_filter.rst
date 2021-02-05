.. _config_http_filters_gzip:

.. warning::

  这个过滤器已弃用，改用
  :ref:`HTTP 压缩过滤器 <config_http_filters_compressor>`。

Gzip
====
Gzip 是一个 HTTP 过滤器，它允许 Envoy 基于客户端请求将从上游服务发过来的数据进行压缩。
在高负载而又不影响响应时间情况下，压缩是很有用的。

配置说明
-------------
* :ref:`v3 API 参考 <envoy_v3_api_msg_extensions.filters.http.gzip.v3.Gzip>`
* 这个过滤器的名称应该配置为 *envoy.filters.http.gzip*。

.. attention::

  位窗 *window bits* 用于告诉压缩算法查找重复字符最长查询窗口。
  基于已知的 zlib 库的缺陷，位窗设置为 8 会导致不正常。
  所以任何小于 9 的配置都会自动设置为 9。
  这个问题可能会在将来版本中修复。

运行时
-------

Gzip 过滤器可以通过压缩器字段内的：:ref:`runtime_enabled <envoy_v3_api_field_extensions.filters.http.gzip.v3.Gzip.compressor>`
字段进行运行时特性的设置。

工作原理
------------
当 gzip 过滤器启用后，会根据请求和应答报文头判断是否需要压缩报文体。
如果请求与应答允许，正文将被压缩，并且将会添加适当的报文头。

这些情况下，压缩将被跳过：

- 请求头中不包含 *accept-encoding*
- 请求头中虽然包含 *accept-encoding* ，但取值中不包含“gzip”或者“\*”
- 请求头中 *accept-encoding* 取值包含“gzip”或者“\*”但权重配置“q=0”
  需要注意的是：“gzip”优先级比“\*”高。例如，当 *accept-encoding*
  取值为“gzip;q=0,\*;q=1”，过滤器将不会压缩。但如果它取值为
  “\*;q=0,gzip;q=1”，过滤器将进行压缩
- 请求头中 *accept-encoding* 中包含其它比“gzip”权重更高的压缩算法，
  且该算法过滤器在处理链中存在
- 响应报文头中包含 *content-encoding*
- 响应报文头中 *cache-control* 取值包含“no-transform”
- 响应报文头中 *transfer-encoding* 取值包含“gzip”
- 响应报文头中 *content-type* 取值不在这些当中：*application/javascript*, *application/json*,
  *application/xhtml+xml*, *image/svg+xml*, *text/css*, *text/html*, *text/plain*,
  *text/xml*
- 响应报文头中不包含 *content-length* 和 *transfer-encoding*
- 响应报文体小于 30 字节 (仅适用于 *transfer-encoding* 取值不为：chunked)

当压缩器生效，将有如下影响：

- 响应中 *content-length* 字段将被去除
- 响应报文头中包含“*transfer-encoding: chunked*”且不包含
  “*content-encoding*”。
- 每个响应报文头中都会带上“*vary: accept-encoding*”。

.. _gzip-statistics:

统计信息
----------

所有配置的 Gzip 过滤器都有以 <stat_prefix>.gzip.* 为根的统计信息，如下：

.. csv-table::
  :header: 名称, 类型, 描述
  :widths: 1, 1, 2

  compressed, Counter, 处理压缩的请求数。
  not_compressed, Counter, 未压缩的请求数。
  no_accept_header, Counter, 未发送 accept 头部的请求数。
  header_identity, Counter, 请求头中包含 *accept-encoding* 的请求数。
  header_gzip, Counter, *accept-encoding* 设置为“gzip”的发送请求数。该指标将弃用，改用 *header_compressor_used*。
  header_compressor_used, Counter, 请求头中 *accept-encoding* 设置为“gzip”的发送请求数。
  header_compressor_overshadowed, Counter, 由于已经匹配链路中其它过滤器而跳过的请求数。
  header_wildcard, Counter, 请求头中 *accept-encoding* 设置为“\*”的请求数。
  header_not_valid, Counter, 请求头中 *accept-encoding* 取值不合法的发送请求数（比如“q=0”或者其它不支持的压缩方式）。
  total_uncompressed_bytes, Counter, 标记为需要压缩但未进行压缩的字节总数。
  total_compressed_bytes, Counter, 标记为需要压缩且已经压缩的字节总数。
  content_length_too_small, Counter, 标记为需要压缩但由于报文字节数太小未进行压缩的请求数。
  not_compressed_etag, Counter, 由于扩展报文头而未进行压缩的请求数。只有当开启 *disable_on_etag_header* 时才有该指标。
