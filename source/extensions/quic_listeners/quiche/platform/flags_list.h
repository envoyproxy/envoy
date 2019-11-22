// This file intentionally does not have header guards, it's intended to be
// included multiple times, each time with a different definition of QUICHE_FLAG.

// NOLINT(namespace-envoy)

// This file is part of the QUICHE platform implementation, and is not to be
// consumed or referenced directly by other Envoy code. It serves purely as a
// porting layer for QUICHE.

// The contents of this file are based off of
// //third_party/quic/core:quic_flags_list in google3, with the addition of
// test-only flags for testing http2 and spdy flags APIs.
// TODO(mpwarres): include generated flags_list.h as part of QUICHE.

#if defined(QUICHE_FLAG)

QUICHE_FLAG(bool, quic_reloadable_flag_advertise_quic_for_https_for_debugips, false, "")

QUICHE_FLAG(bool, quic_reloadable_flag_advertise_quic_for_https_for_external_users, false, "")

QUICHE_FLAG(bool, quic_reloadable_flag_quic_add_upper_limit_of_buffered_control_frames3, true,
            "If true, close connection if there are too many (> 1000) buffered "
            "control frames.")

QUICHE_FLAG(bool, quic_reloadable_flag_quic_allow_backend_set_stream_ttl, false,
            "If true, check backend response header for X-Response-Ttl. If it is "
            "provided, the stream TTL is set. A QUIC stream will be immediately "
            "canceled when tries to write data if this TTL expired.")

QUICHE_FLAG(bool, quic_reloadable_flag_quic_allow_client_enabled_bbr_v2, true,
            "If true, allow client to enable BBRv2 on server via connection "
            "option 'B2ON'.")

QUICHE_FLAG(bool, quic_reloadable_flag_quic_alpn_dispatch, false,
            "Support different QUIC sessions, as indicated by ALPN. Used for QBONE.")

QUICHE_FLAG(bool, quic_reloadable_flag_quic_bbr2_fix_inflight_bounds, false,
            "If true, for QUIC BBRv2: 1) don't grow inflight_hi unless it's "
            "fully used, and 2) cap inflight_lo in PROBE_CRUISE.")

QUICHE_FLAG(bool, quic_reloadable_flag_quic_bbr2_log_bbr2info_in_tracegraf, false,
            "If true, for QUIC BBRv2 flows, log BBRv2-specific information to "
            "tracegraf.")

QUICHE_FLAG(bool, quic_reloadable_flag_quic_bbr_fix_pacing_rate, false,
            "If true, re-calculate pacing rate when cwnd gets bootstrapped.")

QUICHE_FLAG(bool, quic_reloadable_flag_quic_bbr_flexible_app_limited, false,
            "When true and the BBR9 connection option is present, BBR only considers "
            "bandwidth samples app-limited if they're not filling the pipe.")

QUICHE_FLAG(bool, quic_reloadable_flag_quic_bbr_less_probe_rtt, false,
            "Enables 3 new connection options to make PROBE_RTT more aggressive.")

QUICHE_FLAG(bool, quic_reloadable_flag_quic_bbr_no_bytes_acked_in_startup_recovery, false,
            "When in STARTUP and recovery, do not add bytes_acked to QUIC BBR's "
            "CWND in CalculateCongestionWindow()")

QUICHE_FLAG(bool, quic_reloadable_flag_quic_bbr_one_mss_conservation, false,
            "When true, ensure BBR allows at least one MSS to be sent in "
            "response to an ACK in packet conservation.")

QUICHE_FLAG(bool, quic_reloadable_flag_quic_bbr_slower_startup4, false,
            "Enables the BBQ5 connection option, which forces saved aggregation values "
            "to expire when the bandwidth increases more than 25% in QUIC BBR STARTUP.")

QUICHE_FLAG(bool, quic_reloadable_flag_quic_bbr_startup_rate_reduction, false,
            "When true, enables the BBS4 and BBS5 connection options, which reduce "
            "BBR's pacing rate in STARTUP as more losses occur as a fraction of CWND.")

QUICHE_FLAG(bool, quic_reloadable_flag_quic_close_all_encryptions_levels, false,
            "If true, QUIC connection close packet will be sent at all available "
            "encryption levels.")

QUICHE_FLAG(bool, quic_reloadable_flag_quic_close_connection_on_wrong_offset, false,
            "If true, connection will be closed if a stream receives stream "
            "frame or RESET_STREAM frame with bad close offset.")

QUICHE_FLAG(bool, quic_reloadable_flag_quic_coalesce_stream_frames, false,
            "If true, Adjacent stream frames will be combined into one stream "
            "frame before the packet is serialized.")

QUICHE_FLAG(bool, quic_reloadable_flag_quic_combine_generator_and_creator, true,
            "If true, combine QuicPacketGenerator and QuicPacketCreator.")

QUICHE_FLAG(bool, quic_reloadable_flag_quic_conservative_bursts, false,
            "If true, set burst token to 2 in cwnd bootstrapping experiment.")

QUICHE_FLAG(bool, quic_reloadable_flag_quic_conservative_cwnd_and_pacing_gains, false,
            "If true, uses conservative cwnd gain and pacing gain when cwnd gets "
            "bootstrapped.")

QUICHE_FLAG(bool, quic_reloadable_flag_quic_debug_wrong_qos, false,
            "If true, consider getting QoS after stream has been detached as GFE bug.")

QUICHE_FLAG(bool, quic_reloadable_flag_quic_default_to_bbr, true,
            "When true, defaults to BBR congestion control instead of Cubic.")

QUICHE_FLAG(bool, quic_reloadable_flag_quic_default_to_bbr_v2, false,
            "If true, use BBRv2 as the default congestion controller. Takes "
            "precedence over --quic_default_to_bbr.")

QUICHE_FLAG(bool, quic_reloadable_flag_quic_disable_connection_migration_for_udp_proxying, true,
            "If true, GFE disables connection migration in connection option for "
            "proxied sessions.")

QUICHE_FLAG(bool, quic_reloadable_flag_quic_do_not_accept_stop_waiting, false,
            "In v44 and above, where STOP_WAITING is never sent, close the "
            "connection if it's received.")

QUICHE_FLAG(bool, quic_reloadable_flag_quic_donot_process_small_initial_packets, true,
            "If true, server drops client initial packets in datagrams < 1200 bytes.")

QUICHE_FLAG(bool, quic_reloadable_flag_quic_donot_reset_ideal_next_packet_send_time, false,
            "If true, stop resetting ideal_next_packet_send_time_ in pacing sender.")

QUICHE_FLAG(bool, quic_reloadable_flag_quic_enable_ack_decimation, false,
            "Default enables QUIC ack decimation and adds a connection option to "
            "disable it.")

QUICHE_FLAG(bool, quic_reloadable_flag_quic_enable_fifo_write_scheduler, true,
            "If true and FIFO connection option is received, write_blocked_streams "
            "uses FIFO(stream with smallest ID has highest priority) write scheduler.")

QUICHE_FLAG(bool, quic_reloadable_flag_quic_enable_ietf_loss_detection, true,
            "If true, enable IETF loss detection as described in "
            "https://tools.ietf.org/html/draft-ietf-quic-recovery-22#section-6.1.")

QUICHE_FLAG(bool, quic_reloadable_flag_quic_enable_lifo_write_scheduler, true,
            "If true and LIFO connection option is received, write_blocked_streams "
            "uses LIFO(stream with largest ID has highest priority) write scheduler.")

QUICHE_FLAG(bool, quic_reloadable_flag_quic_enable_pcc3, false,
            "If true, enable experiment for testing PCC congestion-control.")

QUICHE_FLAG(bool, quic_reloadable_flag_quic_enable_pto, true, "If true, enable probe timeout.")

QUICHE_FLAG(bool, quic_reloadable_flag_quic_enable_rr_write_scheduler, true,
            "If true, enable HTTP/2 default scheduling(round robin).")

QUICHE_FLAG(bool, quic_reloadable_flag_quic_enable_version_49, false,
            "If true, enable QUIC version 49.")

QUICHE_FLAG(bool, quic_reloadable_flag_quic_enable_version_50, false,
            "If true, enable QUIC version 50.")

QUICHE_FLAG(bool, quic_reloadable_flag_quic_enable_version_99, false, "If true, enable version 99.")

QUICHE_FLAG(bool, quic_reloadable_flag_quic_enabled, false, "")

QUICHE_FLAG(bool, quic_reloadable_flag_quic_fix_bbr_cwnd_in_bandwidth_resumption, true,
            "If true, adjust congestion window when doing bandwidth resumption in BBR.")

QUICHE_FLAG(bool, quic_reloadable_flag_quic_framer_doesnt_create_initial_encrypter, true,
            "If true, QuicFramer does not create an encrypter/decrypter for the "
            "ENCRYPTION_INITIAL level.")

QUICHE_FLAG(bool, quic_reloadable_flag_quic_listener_never_fake_epollout, false,
            "If true, QuicListener::OnSocketIsWritable will always return false, "
            "which means there will never be a fake EPOLLOUT event in the next "
            "epoll iteration.")

QUICHE_FLAG(bool, quic_reloadable_flag_quic_monotonic_epoll_clock, false,
            "If true, QuicEpollClock::Now() will monotonically increase.")

QUICHE_FLAG(bool, quic_reloadable_flag_quic_mtu_discovery_v2, false,
            "If true, enable QUIC MTU discovery version 2.")

QUICHE_FLAG(bool, quic_reloadable_flag_quic_negotiate_ack_delay_time, false,
            "If true, will negotiate the ACK delay time.")

QUICHE_FLAG(bool, quic_reloadable_flag_quic_neuter_handshake_packets_once, false,
            "Call NeuterHandshakePackets() at most once per connection.")

QUICHE_FLAG(bool, quic_reloadable_flag_quic_no_decrease_in_final_offset, false,
            "If true, a stream will be reset if it receives fin that has offset "
            "less than its highest offset.")

QUICHE_FLAG(bool, quic_reloadable_flag_quic_no_dup_experiment_id_2, false,
            "If true, transport connection stats doesn't report duplicated "
            "experiments for same connection.")

QUICHE_FLAG(bool, quic_reloadable_flag_quic_no_stream_data_after_reset, false,
            "If true, QuicStreamSequencer will not take in new data if the "
            "stream is reset.")

QUICHE_FLAG(bool, quic_reloadable_flag_quic_no_v2_scaling_factor, false,
            "When true, don't use an extra scaling factor when reading packets "
            "from QUIC's RX_RING with TPACKET_V2.")

QUICHE_FLAG(bool, quic_reloadable_flag_quic_parse_prox_source_connection_id, true,
            "When true, QuicFramer allows parsing failures of source connection "
            "ID for the PROX version.")

QUICHE_FLAG(bool, quic_reloadable_flag_quic_populate_nonretransmittable_frames, false,
            "If true, populate nonretransmittable frames in SerializedPacket.")

QUICHE_FLAG(bool, quic_reloadable_flag_quic_proxy_write_packed_strings, false,
            "If true, QuicProxyDispatcher will write packed_client_address and "
            "packed_server_vip in TcpProxyHeaderProto.")

QUICHE_FLAG(bool, quic_reloadable_flag_quic_record_frontend_service_vip_mapping, false,
            "If true, for L1 GFE, as requests come in, record frontend service to VIP "
            "mapping which is used to announce VIP in SHLO for proxied sessions. ")

QUICHE_FLAG(bool, quic_reloadable_flag_quic_reject_all_traffic, false, "")

QUICHE_FLAG(bool, quic_reloadable_flag_quic_reject_unprocessable_packets_statelessly, false,
            "If true, do not add connection ID of packets with unknown connection ID "
            "and no version to time wait list, instead, send appropriate responses "
            "depending on the packets' sizes and drop them.")

QUICHE_FLAG(bool, quic_reloadable_flag_quic_require_handshake_confirmation, false,
            "If true, require handshake confirmation for QUIC connections, "
            "functionally disabling 0-rtt handshakes.")

QUICHE_FLAG(bool, quic_reloadable_flag_quic_send_timestamps, false,
            "When the STMP connection option is sent by the client, timestamps "
            "in the QUIC ACK frame are sent and processed.")

QUICHE_FLAG(bool, quic_reloadable_flag_quic_server_push, true,
            "If true, enable server push feature on QUIC.")

QUICHE_FLAG(bool, quic_reloadable_flag_quic_simplify_stop_waiting, true,
            "If true, do not send STOP_WAITING if no_stop_waiting_frame_.")

QUICHE_FLAG(bool, quic_reloadable_flag_quic_skip_packet_number_for_pto, true,
            "If true, skip packet number before sending the last PTO retransmission.")

QUICHE_FLAG(bool, quic_reloadable_flag_quic_stop_reading_when_level_triggered, false,
            "When true, calling StopReading() on a level-triggered QUIC stream "
            "sequencer will cause the sequencer to discard future data.")

QUICHE_FLAG(bool, quic_reloadable_flag_quic_supports_tls_handshake, false,
            "If true, QUIC supports both QUIC Crypto and TLS 1.3 for the "
            "handshake protocol.")

QUICHE_FLAG(bool, quic_reloadable_flag_quic_testonly_default_false, false,
            "A testonly reloadable flag that will always default to false.")

QUICHE_FLAG(bool, quic_reloadable_flag_quic_testonly_default_true, true,
            "A testonly reloadable flag that will always default to true.")

QUICHE_FLAG(bool, quic_reloadable_flag_quic_tracegraf_populate_ack_packet_number, true,
            "If true, populate packet_number of received ACK in tracegraf.")

QUICHE_FLAG(bool, quic_reloadable_flag_quic_treat_queued_packets_as_sent, false,
            "If true, treat queued QUIC packets as sent.")

QUICHE_FLAG(bool, quic_reloadable_flag_quic_unified_iw_options, false,
            "When true, set the initial congestion control window from connection "
            "options in QuicSentPacketManager rather than TcpCubicSenderBytes.")

QUICHE_FLAG(bool, quic_reloadable_flag_quic_use_common_stream_check, false,
            "If true, use common code for checking whether a new stream ID may "
            "be allocated.")

QUICHE_FLAG(bool, quic_reloadable_flag_quic_use_header_stage_idle_list2, false,
            "If true, use header stage idle list for QUIC connections in GFE.")

QUICHE_FLAG(bool, quic_reloadable_flag_quic_use_http2_priority_write_scheduler, true,
            "If true and H2PR connection option is received, write_blocked_streams_ "
            "uses HTTP2 (tree-style) priority write scheduler.")

QUICHE_FLAG(bool, quic_reloadable_flag_quic_use_ip_bandwidth_module, true,
            "If true, use IpBandwidthModule for cwnd bootstrapping if it is "
            "registered.")

QUICHE_FLAG(bool, quic_reloadable_flag_quic_use_leto_key_exchange, false,
            "If true, QUIC will attempt to use the Leto key exchange service and "
            "only fall back to local key exchange if that fails.")

QUICHE_FLAG(bool, quic_reloadable_flag_quic_use_pigeon_sockets, false,
            "Use USPS Direct Path for QUIC egress.")

QUICHE_FLAG(bool, quic_reloadable_flag_quic_use_quic_time_for_received_timestamp, false,
            "If true, use QuicClock::Now() for the fallback source of packet "
            "received time instead of WallNow().")

QUICHE_FLAG(bool, quic_reloadable_flag_quic_version_negotiation_grease, false,
            "When true, QUIC Version Negotiation packets will randomly include "
            "fake versions.")

QUICHE_FLAG(bool, quic_reloadable_flag_send_quic_fallback_server_config_on_leto_error, false,
            "If true and using Leto for QUIC shared-key calculations, GFE will react "
            "to a failure to contact Leto by sending a REJ containing a fallback "
            "ServerConfig, allowing the client to continue the handshake.")

QUICHE_FLAG(bool, quic_restart_flag_do_not_create_raw_socket_selector_if_quic_enabled, false,
            "If true, do not create the RawSocketSelector in "
            "QuicListener::Initialize() if QUIC is disabled by flag.")

QUICHE_FLAG(bool, quic_restart_flag_dont_fetch_quic_private_keys_from_leto, false,
            "If true, GFE will not request private keys when fetching QUIC "
            "ServerConfigs from Leto.")

QUICHE_FLAG(bool, quic_restart_flag_quic_allow_loas_multipacket_chlo, false,
            "If true, inspects QUIC CHLOs for kLOAS and early creates sessions "
            "to allow multi-packet CHLOs")

QUICHE_FLAG(bool, quic_restart_flag_quic_no_cap_net_raw_for_usps_egress, false,
            "If true, gfe2::RawSocket::CapabilityNeeded will return false if "
            "QUIC egress method is USPS.")

QUICHE_FLAG(bool, quic_restart_flag_quic_no_fallback_for_pigeon_socket, false,
            "If true, GFEs using USPS egress will not fallback to raw ip socket.")

QUICHE_FLAG(bool, quic_restart_flag_quic_offload_pacing_to_usps2, false,
            "If true, QUIC offload pacing when using USPS as egress method.")

QUICHE_FLAG(bool, quic_restart_flag_quic_rx_ring_use_tpacket_v3, false,
            "If true, use TPACKET_V3 for QuicRxRing instead of TPACKET_V2.")

QUICHE_FLAG(bool, quic_restart_flag_quic_testonly_default_false, false,
            "A testonly restart flag that will always default to false.")

QUICHE_FLAG(bool, quic_restart_flag_quic_testonly_default_true, true,
            "A testonly restart flag that will always default to true.")

QUICHE_FLAG(bool, quic_restart_flag_quic_use_leto_for_quic_configs, false,
            "If true, use Leto to fetch QUIC server configs instead of using the "
            "seeds from Memento.")

QUICHE_FLAG(bool, quic_restart_flag_quic_use_pigeon_socket_to_backend, false,
            "If true, create a shared pigeon socket for all quic to backend "
            "connections and switch to use it after successful handshake.")

QUICHE_FLAG(bool, quic_allow_chlo_buffering, true,
            "If true, allows packets to be buffered in anticipation of a "
            "future CHLO, and allow CHLO packets to be buffered until next "
            "iteration of the event loop.")

QUICHE_FLAG(bool, quic_disable_pacing_for_perf_tests, false, "If true, disable pacing in QUIC")

QUICHE_FLAG(bool, quic_enforce_single_packet_chlo, true,
            "If true, enforce that QUIC CHLOs fit in one packet")

// Currently, this number is quite conservative. At a hypothetical 1000 qps,
// this means that the longest time-wait list we should see is:
//   200 seconds * 1000 qps = 200000.
// Of course, there are usually many queries per QUIC connection, so we allow a
// factor of 3 leeway.
QUICHE_FLAG(int64_t, // allow-non-std-int
            quic_time_wait_list_max_connections, 600000,
            "Maximum number of connections on the time-wait list. "
            "A negative value implies no configured limit.")

QUICHE_FLAG(int64_t, // allow-non-std-int
            quic_time_wait_list_seconds, 200,
            "Time period for which a given connection_id should live in "
            "the time-wait state.")

QUICHE_FLAG(double, quic_bbr_cwnd_gain, 2.0f,
            "Congestion window gain for QUIC BBR during PROBE_BW phase.")

QUICHE_FLAG(int32_t, // allow-non-std-int
            quic_buffered_data_threshold, 8 * 1024,
            "If buffered data in QUIC stream is less than this "
            "threshold, buffers all provided data or asks upper layer for more data")

QUICHE_FLAG(int32_t, // allow-non-std-int
            quic_send_buffer_max_data_slice_size, 4 * 1024,
            "Max size of data slice in bytes for QUIC stream send buffer.")

QUICHE_FLAG(int32_t, // allow-non-std-int
            quic_lumpy_pacing_size, 2,
            "Number of packets that the pacing sender allows in bursts during "
            "pacing. This flag is ignored if a flow's estimated bandwidth is "
            "lower than 1200 kbps.")

QUICHE_FLAG(double, quic_lumpy_pacing_cwnd_fraction, 0.25f,
            "Congestion window fraction that the pacing sender allows in bursts "
            "during pacing.")

QUICHE_FLAG(int32_t, // allow-non-std-int
            quic_max_pace_time_into_future_ms, 10,
            "Max time that QUIC can pace packets into the future in ms.")

QUICHE_FLAG(double, quic_pace_time_into_future_srtt_fraction,
            0.125f, // One-eighth smoothed RTT
            "Smoothed RTT fraction that a connection can pace packets into the future.")

QUICHE_FLAG(bool, quic_export_server_num_packets_per_write_histogram, false,
            "If true, export number of packets written per write operation histogram.")

QUICHE_FLAG(bool, quic_disable_version_negotiation_grease_randomness, false,
            "If true, use predictable version negotiation versions.")

QUICHE_FLAG(int64_t, // allow-non-std-int
            quic_max_tracked_packet_count, 10000, "Maximum number of tracked packets.")

QUICHE_FLAG(bool, quic_prober_uses_length_prefixed_connection_ids, false,
            "If true, QuicFramer::WriteClientVersionNegotiationProbePacket uses "
            "length-prefixed connection IDs.")

QUICHE_FLAG(bool, quic_client_convert_http_header_name_to_lowercase, true,
            "If true, HTTP request header names sent from QuicSpdyClientBase(and "
            "descendents) will be automatically converted to lower case.")

QUICHE_FLAG(int32_t, // allow-non-std-int
            quic_bbr2_default_probe_bw_base_duration_ms, 2000,
            "The default minimum duration for BBRv2-native probes, in milliseconds.")

QUICHE_FLAG(int32_t, // allow-non-std-int
            quic_bbr2_default_probe_bw_max_rand_duration_ms, 1000,
            "The default upper bound of the random amount of BBRv2-native "
            "probes, in milliseconds.")

QUICHE_FLAG(int32_t, // allow-non-std-int
            quic_bbr2_default_probe_rtt_period_ms, 10000,
            "The default period for entering PROBE_RTT, in milliseconds.")

QUICHE_FLAG(double, quic_bbr2_default_loss_threshold,
            0.3, // Changed from 0.02 for YouTube and Search experiments.
            "The default loss threshold for QUIC BBRv2, should be a value "
            "between 0 and 1.")

QUICHE_FLAG(int32_t, // allow-non-std-int
            quic_bbr2_default_startup_full_loss_count, 8,
            "The default minimum number of loss marking events to exit STARTUP.")

QUICHE_FLAG(double, quic_bbr2_default_inflight_hi_headroom,
            0.01, // Changed from 0.15 for YouTube and Search experiments.
            "The default fraction of unutilized headroom to try to leave in path "
            "upon high loss.")

QUICHE_FLAG(double, quic_ack_aggregation_bandwidth_threshold, 1.0,
            "If the bandwidth during ack aggregation is smaller than (estimated "
            "bandwidth * this flag), consider the current aggregation completed "
            "and starts a new one.")

QUICHE_FLAG(int32_t, // allow-non-std-int
            quic_anti_amplification_factor, 3,
            "Anti-amplification factor. Before address validation, server will "
            "send no more than factor times bytes received.")

QUICHE_FLAG(int32_t, // allow-non-std-int
            quic_max_buffered_crypto_bytes,
            16 * 1024, // 16 KB
            "The maximum amount of CRYPTO frame data that can be buffered.")

QUICHE_FLAG(bool, quic_allow_http3_priority, false, "If true, support HTTP/3 priority")

QUICHE_FLAG(int32_t, // allow-non-std-int
            quic_max_aggressive_retransmittable_on_wire_ping_count, 0,
            "If set to non-zero, the maximum number of consecutive pings that "
            "can be sent with aggressive initial retransmittable on wire timeout "
            "if there is no new data received. After which, the timeout will be "
            "exponentially back off until exceeds the default ping timeout.")

QUICHE_FLAG(int32_t, // allow-non-std-int
            quic_max_congestion_window, 2000, "The maximum congestion window in packets.")

QUICHE_FLAG(bool, http2_reloadable_flag_http2_testonly_default_false, false,
            "A testonly reloadable flag that will always default to false.")

QUICHE_FLAG(bool, http2_restart_flag_http2_testonly_default_false, false,
            "A testonly restart flag that will always default to false.")

QUICHE_FLAG(bool, spdy_reloadable_flag_spdy_testonly_default_false, false,
            "A testonly reloadable flag that will always default to false.")

QUICHE_FLAG(bool, spdy_restart_flag_spdy_testonly_default_false, false,
            "A testonly restart flag that will always default to false.")

#endif
