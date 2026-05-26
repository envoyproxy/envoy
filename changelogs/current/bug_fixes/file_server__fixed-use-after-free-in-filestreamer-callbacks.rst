Fixed a use-after-free in the ``file_server`` HTTP filter when a request is cancelled
(downstream RST, stream timeout, etc.) before the underlying ``AsyncFileManager::stat``
or ``read`` callback fires. ``FileStreamer`` previously captured ``[this]`` into the
async-file callbacks, which left a dangling pointer if the ``FileStreamer`` was
destroyed while a stat or read was still in flight or already queued on the owner
dispatcher. The callbacks now use ``cancelWrapped`` (``source/common/common/cancel_wrapper.h``):
the in-flight file operation is aborted via the file-side cancel, and any callback
already dispatched to the worker's event loop short-circuits before dereferencing the
freed ``FileStreamer``.
