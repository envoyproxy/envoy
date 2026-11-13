Fixed a bug where a filter manager body buffer dropping below its low watermark after a too large
body had triggered a local reply (rather than backpressure) released backpressure that was never
applied, e.g. resuming reads on the connection or calling the downstream low watermark callbacks
without a matching high watermark.
