// This file intentionally does not have header guards, it's intended to be
// included multiple times, each time with a different definition of QUICHE_FLAG.

// NOLINT(namespace-envoy)

// This file is part of the QUICHE platform implementation, and is not to be
// consumed or referenced directly by other Envoy code. It serves purely as a
// porting layer for QUICHE.

#if defined(QUICHE_FLAG)

QUICHE_FLAG(bool, quic_allow_chlo_buffering, true,
            "If true, allows packets to be buffered in anticipation of a "
            "future CHLO, and allow CHLO packets to be buffered until next "
            "iteration of the event loop.")

QUICHE_FLAG(bool, quic_disable_pacing_for_perf_tests, false, "If true, disable pacing in QUIC")

QUICHE_FLAG(bool, quic_enforce_single_packet_chlo, true,
            "If true, enforce that QUIC CHLOs fit in one packet")

QUICHE_FLAG(int64_t, quic_time_wait_list_max_connections, 600000,
            "Maximum number of connections on the time-wait list.  "
            "A negative value implies no configured limit.")

QUICHE_FLAG(int64_t, quic_time_wait_list_seconds, 200,
            "Time period for which a given connection_id should live in "
            "the time-wait state.")

QUICHE_FLAG(double, quic_bbr_cwnd_gain, 2.0f,
            "Congestion window gain for QUIC BBR during PROBE_BW phase.")

QUICHE_FLAG(int32_t, quic_buffered_data_threshold, 8 * 1024,
            "If buffered data in QUIC stream is less than this "
            "threshold, buffers all provided data or asks upper layer for more data")

QUICHE_FLAG(int32_t, quic_send_buffer_max_data_slice_size, 4 * 1024,
            "Max size of data slice in bytes for QUIC stream send buffer.")

QUICHE_FLAG(bool, quic_supports_tls_handshake, false,
            "If true, QUIC supports both QUIC Crypto and TLS 1.3 for the "
            "handshake protocol")

QUICHE_FLAG(int32_t, quic_lumpy_pacing_size, 1,
            "Number of packets that the pacing sender allows in bursts during pacing.")

QUICHE_FLAG(double, quic_lumpy_pacing_cwnd_fraction, 0.25f,
            "Congestion window fraction that the pacing sender allows in bursts "
            "during pacing.")

QUICHE_FLAG(int32_t, quic_max_pace_time_into_future_ms, 10,
            "Max time that QUIC can pace packets into the future in ms.")

QUICHE_FLAG(double, quic_pace_time_into_future_srtt_fraction, 0.125f,
            "Smoothed RTT fraction that a connection can pace packets into the future.")

QUICHE_FLAG(bool, quic_reloadable_flag_enable_quic_stateless_reject_support, true,
            "Enables server-side support for QUIC stateless rejects.")

QUICHE_FLAG(bool, quic_reloadable_flag_quic_always_reset_short_header_packets, true,
            "If true, instead of send encryption none termination packets, send stateless reset in "
            "reponse to short headers.")

QUICHE_FLAG(bool, quic_reloadable_flag_quic_bbr_app_limited_recovery, false,
            "When you're app-limited entering recovery, stay app-limited until you exit recovery "
            "in QUIC BBR.")

QUICHE_FLAG(bool, quic_reloadable_flag_quic_bbr_flexible_app_limited, false,
            "When true and the BBR9 connection option is present, BBR only considers bandwidth "
            "samples app-limited if they're not filling the pipe.")

QUICHE_FLAG(bool, quic_reloadable_flag_quic_bbr_less_probe_rtt, false,
            "Enables 3 new connection options to make PROBE_RTT more aggressive.")

QUICHE_FLAG(bool, quic_reloadable_flag_quic_bbr_no_bytes_acked_in_startup_recovery, false,
            "When in STARTUP and recovery, do not add bytes_acked to QUIC BBR's CWND in "
            "CalculateCongestionWindow()")

QUICHE_FLAG(bool, quic_reloadable_flag_quic_bbr_one_mss_conservation, false,
            "When true, ensure BBR allows at least one MSS to be sent in response to an ACK in "
            "packet conservation.")

QUICHE_FLAG(bool, quic_reloadable_flag_quic_bbr_slower_startup3, false,
            "Add 3 connection options to decrease the pacing and CWND gain in QUIC BBR STARTUP.")

QUICHE_FLAG(bool, quic_reloadable_flag_quic_bbr_slower_startup4, false,
            "Enables the BBQ5 connection option, which forces saved aggregation values to expire "
            "when the bandwidth increases more than 25% in QUIC BBR STARTUP.")

QUICHE_FLAG(bool, quic_reloadable_flag_quic_bbr_startup_rate_reduction, false,
            "When true, enables the BBS4 and BBS5 connection options, which reduce BBR's pacing "
            "rate in STARTUP as more losses occur as a fraction of CWND.")

QUICHE_FLAG(bool, quic_reloadable_flag_quic_call_write_mem_slices, true,
            "If true, QuicSpdyStream::WritevBody will convert iov to memslices and then call "
            "WriteMemSlices directly.")

QUICHE_FLAG(
    bool, quic_reloadable_flag_quic_connection_do_not_add_to_write_blocked_list_if_disconnected,
    true,
    "If true, disconnected quic connection will not be added to dispatcher's write blocked list.")

QUICHE_FLAG(bool, quic_reloadable_flag_quic_default_to_bbr, true,
            "When true, defaults to BBR congestion control instead of Cubic.")

QUICHE_FLAG(bool, quic_reloadable_flag_quic_deprecate_ack_bundling_mode, false,
            "If true, stop using AckBundling mode to send ACK, also deprecate ack_queued from "
            "QuicConnection.")

QUICHE_FLAG(bool, quic_reloadable_flag_quic_disable_version_39, false,
            "If true, disable QUIC version 39.")

QUICHE_FLAG(bool, quic_reloadable_flag_quic_donot_reset_ideal_next_packet_send_time, false,
            "If true, stop resetting ideal_next_packet_send_time_ in pacing sender.")

QUICHE_FLAG(bool, quic_reloadable_flag_quic_eighth_rtt_loss_detection, false,
            "When true, the LOSS connection option allows for 1/8 RTT of reording instead of the "
            "current 1/8th threshold which has been found to be too large for fast loss recovery.")

QUICHE_FLAG(bool, quic_reloadable_flag_quic_enable_ack_decimation, false,
            "Default enables QUIC ack decimation and adds a connection option to disable it.")

QUICHE_FLAG(bool, quic_reloadable_flag_quic_enable_pcc3, false,
            "If true, enable experiment for testing PCC congestion-control.")

QUICHE_FLAG(bool, quic_reloadable_flag_quic_enable_version_43, true,
            "If true, enable QUIC version 43.")

QUICHE_FLAG(bool, quic_reloadable_flag_quic_enable_version_44, true,
            "If true, enable version 44 which uses IETF header format.")

QUICHE_FLAG(bool, quic_reloadable_flag_quic_enable_version_46, true,
            "If true, enable QUIC version 46.")

QUICHE_FLAG(bool, quic_reloadable_flag_quic_enable_version_47, false,
            "If true, enable QUIC version 47 which adds CRYPTO frames.")

QUICHE_FLAG(bool, quic_reloadable_flag_quic_enable_version_99, false, "If true, enable version 99.")

QUICHE_FLAG(bool, quic_reloadable_flag_quic_faster_interval_add_in_sequence_buffer, false,
            "If true, QuicStreamSequencerBuffer will switch to a new "
            "QuicIntervalSet::AddOptimizedForAppend method in OnStreamData().")

QUICHE_FLAG(bool, quic_reloadable_flag_quic_fix_adaptive_time_loss, false,
            "Simplify QUIC's adaptive time loss detection to measure the necessary reordering "
            "window for every spurious retransmit.")

QUICHE_FLAG(bool, quic_reloadable_flag_quic_fix_config_rotation, true,
            "If true, QuicCryptoServerConfig will correctly rotate configs based on primary time.")

QUICHE_FLAG(bool, quic_reloadable_flag_quic_fix_has_pending_crypto_data, false,
            "If true, QuicSession::HasPendingCryptoData checks whether the crypto stream's send "
            "buffer is empty. This flag fixes a bug where the retransmission alarm mode is wrong "
            "for the first CHLO packet.")

QUICHE_FLAG(bool, quic_reloadable_flag_quic_fix_spurious_ack_alarm, false,
            "If true, do not schedule ack alarm if should_send_ack is set in the generator.")

QUICHE_FLAG(bool, quic_reloadable_flag_quic_fix_termination_packets, false,
            "If true, GFE time wait list will send termination packets based on current packet's "
            "encryption level.")

QUICHE_FLAG(bool, quic_reloadable_flag_quic_fix_time_of_first_packet_sent_after_receiving, false,
            "When true, fix initialization and updating of "
            "|time_of_first_packet_sent_after_receiving_| in QuicConnection.")

QUICHE_FLAG(bool, quic_reloadable_flag_quic_log_cert_name_for_empty_sct, true,
            "If true, log leaf cert subject name into warning log.")

QUICHE_FLAG(bool, quic_reloadable_flag_quic_monotonic_epoll_clock, false,
            "If true, QuicEpollClock::Now() will monotonically increase.")

QUICHE_FLAG(bool, quic_reloadable_flag_quic_no_client_conn_ver_negotiation, false,
            "If true, a client connection would be closed when a version negotiation packet is "
            "received. It would be the higher layer's responsibility to do the reconnection.")

QUICHE_FLAG(bool, quic_reloadable_flag_quic_optimize_inflight_check, false,
            "Stop checking QuicUnackedPacketMap::HasUnackedRetransmittableFrames and instead rely "
            "on the existing check that bytes_in_flight > 0")

QUICHE_FLAG(bool, quic_reloadable_flag_quic_require_handshake_confirmation, false,
            "If true, require handshake confirmation for QUIC connections, functionally disabling "
            "0-rtt handshakes.")

QUICHE_FLAG(bool, quic_reloadable_flag_quic_rpm_decides_when_to_send_acks, false,
            "If both this flag and gfe2_reloadable_flag_quic_deprecate_ack_bundling_mode are true, "
            "QuicReceivedPacketManager decides when to send ACKs.")

QUICHE_FLAG(bool, quic_reloadable_flag_quic_send_timestamps, false,
            "When the STMP connection option is sent by the client, timestamps in the QUIC ACK "
            "frame are sent and processed.")

QUICHE_FLAG(bool, quic_reloadable_flag_quic_set_transmission_type_for_next_frame, true,
            "If true, QuicPacketCreator::SetTransmissionType will set the transmission type of the "
            "next successfully added frame.")

QUICHE_FLAG(
    bool, quic_reloadable_flag_quic_simplify_build_connectivity_probing_packet, true,
    "If true, simplifies the implementation of QuicFramer::BuildConnectivityProbingPacket().")

QUICHE_FLAG(bool, quic_reloadable_flag_quic_stop_reading_when_level_triggered, false,
            "When true, calling StopReading() on a level-triggered QUIC stream sequencer will "
            "cause the sequencer to discard future data.")

QUICHE_FLAG(bool, quic_reloadable_flag_quic_unified_iw_options, false,
            "When true, set the initial congestion control window from connection options in "
            "QuicSentPacketManager rather than TcpCubicSenderBytes.")

QUICHE_FLAG(bool, quic_reloadable_flag_quic_use_cheap_stateless_rejects, true,
            "If true, QUIC will use cheap stateless rejects without creating a full connection. "
            "Prerequisite: --quic_allow_chlo_buffering has to be true.")

QUICHE_FLAG(bool, quic_reloadable_flag_quic_use_common_stream_check, false,
            "If true, use common code for checking whether a new stream ID may be allocated.")

QUICHE_FLAG(
    bool, quic_reloadable_flag_quic_use_new_append_connection_id, false,
    "When true QuicFramer will use AppendIetfConnectionIdsNew instead of AppendIetfConnectionId.")

QUICHE_FLAG(bool, quic_reloadable_flag_quic_use_uber_loss_algorithm, false,
            "If true, use one loss algorithm per encryption level.")

QUICHE_FLAG(bool, quic_restart_flag_quic_enable_accept_random_ipn, false,
            "Allow QUIC to accept initial packet numbers that are random, not 1.")

QUICHE_FLAG(bool, quic_restart_flag_quic_no_server_conn_ver_negotiation2, false,
            "If true, dispatcher passes in a single version when creating a server connection, "
            "such that version negotiation is not supported in connection.")

QUICHE_FLAG(bool, quic_restart_flag_quic_offload_pacing_to_usps2, false,
            "If true, QUIC offload pacing when using USPS as egress method.")

QUICHE_FLAG(
    bool, quic_restart_flag_quic_uint64max_uninitialized_pn, true,
    "If true, use numeric_limits<uint64_t>::max() to represent uninitialized packet number.")

QUICHE_FLAG(bool, http2_reloadable_flag_http2_testonly_default_false, false,
            "A testonly reloadable flag that will always default to false.")

QUICHE_FLAG(bool, http2_restart_flag_http2_testonly_default_false, false,
            "A testonly restart flag that will always default to false.")

QUICHE_FLAG(bool, spdy_reloadable_flag_spdy_testonly_default_false, false,
            "A testonly reloadable flag that will always default to false.")

QUICHE_FLAG(bool, spdy_restart_flag_spdy_testonly_default_false, false,
            "A testonly restart flag that will always default to false.")

QUICHE_FLAG(bool, quic_reloadable_flag_quic_testonly_default_false, false,
            "A testonly reloadable flag that will always default to false.")

QUICHE_FLAG(bool, quic_reloadable_flag_quic_testonly_default_true, true,
            "A testonly reloadable flag that will always default to true.")

QUICHE_FLAG(bool, quic_restart_flag_quic_testonly_default_false, false,
            "A testonly restart flag that will always default to false.")

QUICHE_FLAG(bool, quic_restart_flag_quic_testonly_default_true, true,
            "A testonly restart flag that will always default to true.")

#endif
