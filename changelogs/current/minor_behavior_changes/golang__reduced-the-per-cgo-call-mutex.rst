Reduced the per-cgo-call mutex acquisition on the Golang HTTP filter by making the
``has_destroyed_`` flag a ``std::atomic<bool>``. CAPI methods whose only Envoy-side work is
Filter-owned or runs on the worker thread (``setHeader``, ``removeHeader``, ``setTrailer``,
``removeTrailer``, ``addData``, ``injectData``, ``continueStatus``, ``sendLocalReply``,
``setBufferHelper``, ``copyBuffer``, ``drainBuffer``, ``setUpstreamOverrideHost``,
``clearRouteCache``, ``setDynamicMetadata``, ``setStringFilterState``) no longer take the
mutex, eliminating an uncontended atomic compare-and-swap pair on every such call. The
mutex is retained on the CAPI methods that inline-dereference Envoy-stream-owned objects
from off-thread (``getHeader``, ``copyHeaders``, ``copyTrailers``, ``getIntegerValue``,
``setDrainConnectionUponCompletion``) where it serialises against ``onDestroy`` to prevent
the worker thread from freeing the underlying header map or ``StreamInfo`` mid-access, and
on the five methods that write to the per-request ``strValue`` scratch buffer
(``getStringValue``, ``getDynamicMetadata``, ``getStringFilterState``, ``getStringProperty``,
``getSecret``).
