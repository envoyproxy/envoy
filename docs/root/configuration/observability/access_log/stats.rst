.. _config_access_log_stats:

统计
==========

目前只有基于 gRPC 和文件的访问日志有统计。

gRPC 访问日志统计
--------------------------

gRPC访问日志的统计以 *access_logs.grpc_access_log.* 为根，统计信息如下:

.. csv-table::
   :header: 名字, 类型, 描述
   :widths: 1, 1, 2

   logs_written, Counter, 发送到日志处理器未被丢弃的日志条目总数。这并不意味着日志已经刷新到gRPC端点。
   logs_dropped, Counter, 由于网络或 HTTP/2 阻塞而丢弃的日志条目总数。


File 访问日志统计
--------------------------

文件访问日志的统计以 *filesystem.* 为根的命名空间。

.. csv-table::
  :header: 名字, 类型, 描述
  :widths: 1, 1, 2

  write_buffered, Counter, 数据被移入 Envoy 内部刷新缓存区的总次数
  write_completed, Counter, 文件写成功的总次数
  write_failed, Counter, 文件写操作过程中出错的总次数
  flushed_by_timer, Counter, 内部刷新缓冲区由于刷新定时器到期而写文件的总次数
  reopen_failed, Counter, 文件打开失败的总次数
  write_total_buffered, Gauge, 当前内部刷新缓冲区的总大小（以字节为单位）
