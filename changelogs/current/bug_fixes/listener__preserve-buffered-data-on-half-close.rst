listener: fixed buffered data being discarded when a listener filter receives combined read and close
notifications, such as when a peer half-closes an internal-listener connection before protocol
inspection completes.
