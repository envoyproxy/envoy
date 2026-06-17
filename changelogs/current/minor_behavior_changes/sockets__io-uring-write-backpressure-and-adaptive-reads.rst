Existing :ref:`io_uring <envoy_v3_api_field_extensions.network.socket_interface.v3.DefaultSocketInterface.io_uring_options>`
deployments now apply write backpressure by default, so a write can return ``EAGAIN`` once the write
buffer exceeds ``write_high_watermark_bytes`` (128 KiB) and resume at or below
``write_low_watermark_bytes`` (16 KiB). The ``readv``-based read buffer also grows adaptively up to
16 times ``read_buffer_size``
for large transfers.
