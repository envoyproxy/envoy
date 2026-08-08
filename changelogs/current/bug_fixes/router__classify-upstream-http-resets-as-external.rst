Fixed upstream HTTP resets being reported as locally originated failures to outlier detection.
Peer-generated resets and upstream protocol errors are now reported as externally originated
failures when split external and local origin errors are enabled.
