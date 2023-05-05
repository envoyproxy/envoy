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
   cx_data_segments_delivered, Counter, Data segments delivered to the receiver including retransmits
   cx_reordering, Counter, Maximum observed reordering distance
   cx_tx_unsent_bytes, Gauge, Bytes which Envoy has sent to the operating system which have not yet been sent
   cx_tx_unacked_segments, Gauge, Segments which have been transmitted that have not yet been acknowledged
   cx_rto_us, Gauge, Retransmission timeout
   cx_ato_us, Gauge, Ack timeout
   cx_lost, Gauge, Lost segments
   cx_tx_ssthreshold, Gauge, Sender's slow start threshold expressed in packets
   cx_rx_ssthreshold, Gauge, Maximum number of bytes currently advertised as the TCP receive window
   cx_tx_mss_bytes, Gauge, Maximum segment size in bytes for sending
   cx_rx_mss_bytes, Gauge, Maximum segment size in bytes for receiving
   cx_advmss_bytes, Gauge, Advertised in the maximum segment size in bytes
   cx_pmtu_bytes, Gauge, Bytes the TCP stack believes can be transmitted in one IP packet without fragmentation
   cx_tx_percent_retransmitted_segments, Histogram, Percent of segments on a connection which were retransmistted
   cx_rtt_us, Histogram, Smoothed round trip time estimate in microseconds
   cx_rtt_variance_us, Histogram, Estimated variance in microseconds of the round trip time. Higher values indicated more variability.
   cx_rcv_rtt_us, Histogram, Receiver side round trip time estimate in microseconds
   cx_tx_window_scale, Histogram, Shift value to scale send window
   cx_rx_window_scale, Histogram, Shift value to scale receive window
   cx_congestion_window, Histogram, urrent congestion window in bytes for sending data
   cx_pacing_rate, Histogram, Pacing rate in MB per second
   cx_delivery_rate, Histogram, Observed maximum delivery rate in MB per second