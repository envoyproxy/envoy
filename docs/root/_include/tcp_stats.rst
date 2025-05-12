.. note::
  These metrics are provided by the operating system. Due to differences in operating system metrics available and the methodology
  used to take measurements, the values may not be consistent across different operating systems or versions of the same operating
  system.

.. csv-table::
   :header: Name, Type, Description
   :widths: 1, 1, 2

   cx_tx_segments, Counter, Total TCP segments transmitted
   cx_rx_segments, Counter, Total TCP segments received
   cx_tx_data_segments, Counter, Total TCP segments with a non-zero data length transmitted
   cx_rx_data_segments, Counter, Total TCP segments with a non-zero data length received
   cx_tx_retransmitted_segments, Counter, Total TCP segments retransmitted
   cx_rx_bytes_received, Counter, Total payload bytes received for which TCP acknowledgments have been sent.
   cx_tx_bytes_sent, Counter, Total payload bytes transmitted (including retransmitted bytes).
   cx_tx_unsent_bytes, Gauge, Bytes which Envoy has sent to the operating system which have not yet been sent
   cx_tx_unacked_segments, Gauge, Segments which have been transmitted that have not yet been acknowledged
   cx_tx_percent_retransmitted_segments, Histogram, Percent of segments on a connection which were retransmistted
   cx_rtt_us, Histogram, Smoothed round trip time estimate in microseconds
   cx_rtt_variance_us, Histogram, Estimated variance in microseconds of the round trip time. Higher values indicated more variability.
