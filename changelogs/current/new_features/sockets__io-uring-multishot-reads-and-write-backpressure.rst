The :ref:`io_uring <envoy_v3_api_field_extensions.network.socket_interface.v3.DefaultSocketInterface.io_uring_options>`
socket interface now supports :ref:`multishot reads <envoy_v3_api_field_extensions.network.socket_interface.v3.IoUringOptions.enable_multishot_receive>`
backed by a kernel-provided buffer ring on Linux kernel 6.0 or later, and applies write backpressure
through the new :ref:`write_high_watermark_bytes <envoy_v3_api_field_extensions.network.socket_interface.v3.IoUringOptions.write_high_watermark_bytes>`
and :ref:`write_low_watermark_bytes <envoy_v3_api_field_extensions.network.socket_interface.v3.IoUringOptions.write_low_watermark_bytes>`
options. The ``readv``-based read path now grows its buffer adaptively for large transfers.
