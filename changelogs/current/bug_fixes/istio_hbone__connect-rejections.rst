Added an application connection pool adapter for the contrib Istio HBONE shim.
Permanent CONNECT rejections, including authorization denials, terminate HTTP or
TCP requests without retry or outlier failure reporting. CONNECT server errors
retain connection-failure retry behavior. A wrapped TLS transport preserves outer
CONNECT rejections through double HBONE.
