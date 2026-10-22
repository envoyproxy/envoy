Fixed HTTP protocol errors when the HBONE origination shim is used with Istio's
baggage peer-metadata filters. The upstream filter now recognizes both shim
origination listener names and consumes the metadata preamble before forwarding
the application response.
