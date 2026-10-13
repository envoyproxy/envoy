.. _config_http_filters_file_system_buffer:

File System Buffer
==================

The file system buffer filter can be used to stop filter iteration and wait for a fully buffered
complete request, or to provide a buffer into which upstream or downstream can empty their data
faster than their recipient reads it.

This is useful in different situations including protecting some applications from having to deal
with partial requests or high network latency.

If enabled the file system buffer filter can populate (or correct) the content-length header
if it is not present or differs from the actual size in the request. This behavior can be separately
enabled for requests or responses, via the listener or per-route configuration of the filter.

.. note::

 This filter is not yet supported on Windows.

Configuration
-------------

* This filter should be configured with the type URL ``type.googleapis.com/envoy.extensions.filters.http.file_system_buffer.v3.FileSystemBufferFilterConfig``.
* :ref:`v3 API reference <envoy_v3_api_msg_extensions.filters.http.file_system_buffer.v3.FileSystemBufferFilterConfig>`
* This filter should not be active on the same stream as :ref:`Buffer filter <config_http_filters_buffer>`, or any other
  filter that changes the default buffering behavior, as stream watermarks will behave
  unpredictably with multiple buffer-altering filters. (There is also no need to have both together,
  as this filter offers the same features.)

What is it good for?
--------------------

There are several use-cases for this filter.

1. To shield a server from intentional or unintentional denial of service via slow requests. Normal
   requests open a connection and stream the request. If the client streams the request very slowly,
   the server may have its limited resources held by that connection for the duration of the slow
   stream. With one of the "always buffer" configurations for requests, the connection to the server
   is postponed until the entire request has been received by Envoy, guaranteeing that from the
   server's perspective the request will be as fast as Envoy can deliver it, rather than at the speed
   of the client.

2. Similarly, to shield a server from clients receiving a response slowly. For this case, an "always
   buffer" configuration is not a requirement. The standard Envoy behavior already implements a
   configurable memory buffer for this purpose, that will allow the server to flush until that buffer
   hits the "high watermark" that provokes a request for the server to slow down.

   ``FileSystemBufferFilter`` attempts to take over when that "high watermark" event occurs, slowing
   delivery to the client without slowing receipt from the server. To avoid the high memory usage
   that behavior would usually entail, the filter offloads excess buffered data to the filesystem,
   and reloads it when the client is ready to receive it.

3. The filter optionally injects an accurate ``content-length`` header, which requires buffering the
   entire request or response. This aspect duplicates the behavior of ``buffer_filter`` - having both
   of these filters in the same chain doesn't make sense, as ``buffer_filter`` explicitly requires
   that the entire request/response is buffered in memory.

.. note::

  In the current implementation, use-case 2 is implemented imperfectly - when the receiving end
  sends the high watermark event, this also pauses reading from the providing end. An update is in the
  works to make it possible for the filter to intercept that signal.

.. note::

  Though the filter can theoretically be configured to perform the same behavior symmetrically,
  Envoy currently only provides ``onAboveWriteBufferHighWatermark`` and ``onBelowWriteBufferLowWatermark``
  events on the response stream - this means requests will always flow immediately through the filter
  into the regular filter chain's memory buffer unless using a configuration that requires that the
  entire request be buffered. An update is in the works to make these signals also available on
  request streams.

Under the hood
--------------

The behavior of the buffer attempts to minimize the amount of disk activity and maximize throughput.
To support this, it follows the following rules when in a streaming configuration.

1. If the configured memory limit is enough to contain the request/response (or the amount of it
   received that hasn't yet been streamed to the client), the filter performs neither storage
   activity nor buffer copies - received data is simply moved through the filter.
2. When data can be sent, the memory buffer is not full, and anything is buffered on storage
   (or when the immediately next piece of data to be sent is on storage), the *earliest* fragment
   of buffer is loaded back from storage.
3. When the memory limit is exceeded, the *latest* memory fragment of buffer is sent to storage.
   This way the part of the buffer that remains in memory is the part that will be needed soonest.

Storage writes go to an unlinked file, so in the event of a hard restart there is nothing to clean up.
