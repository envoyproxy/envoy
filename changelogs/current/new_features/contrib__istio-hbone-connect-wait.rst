Added experimental :ref:`Istio HBONE CONNECT wait <config_istio_hbone>` extensions
that defer internal connection readiness and application I/O until HTTP/2 CONNECT
acceptance. Transient establishment failures retain connection-failure retries;
permanent CONNECT rejections terminate HTTP or TCP requests without retry or outlier
failure reporting. Single and double HBONE origination are supported, including
wrapped TLS and Istio's baggage peer-metadata exchange.
