Fixed a crash in the dynamic modules HTTP filter. Tearing the filter chain down clears the filter's
decoder and encoder callbacks, but the body-buffer ABI callbacks dereferenced them without a null
check, so a module that touched a buffered body after a terminal operation (for example
``continue_decoding``, ``reset_stream``, ``send_go_away_and_close`` or ``recreate_stream``) crashed
Envoy. Those callbacks now report failure instead.
