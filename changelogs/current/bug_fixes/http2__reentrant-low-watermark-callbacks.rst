Fixed HTTP/2 connection write-buffer low-watermark callback delivery when a callback reentrantly
encodes data. Previously, reordering the active-stream list during callback delivery could notify
one stream twice and skip another, leading to a stalled response.
