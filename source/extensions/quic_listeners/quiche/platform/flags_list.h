// This file intentionally does not have header guards. It is intended to be
// included multiple times, each time with a different definition of
// QUICHE_FLAG.

// NOLINT(namespace-envoy)

// This file is part of the QUICHE platform implementation, and is not to be
// consumed or referenced directly by other Envoy code. It serves purely as a
// porting layer for QUICHE.

#if defined(QUICHE_FLAG)

QUICHE_FLAG(
    bool, http2_reloadable_flag_http2_backend_alpn_failure_error_code, false,
    "If true, the GFE will return a new ResponseCodeDetails error when ALPN to the backend fails.")

QUICHE_FLAG(bool, http2_reloadable_flag_http2_security_requirement_for_client3, false,
            "If true, check whether client meets security requirements during SSL handshake. If "
            "flag is true and client does not meet security requirements, do not negotiate HTTP/2 "
            "with client or terminate the session with SPDY_INADEQUATE_SECURITY if HTTP/2 is "
            "already negotiated. The spec contains both cipher and TLS version requirements.")

QUICHE_FLAG(bool, http2_reloadable_flag_permissive_http2_switch, false,
            "If true, the GFE allows both HTTP/1.0 and HTTP/1.1 versions in HTTP/2 upgrade "
            "requests/responses.")

QUICHE_FLAG(bool, quic_reloadable_flag_advertise_quic_for_https_for_debugips, false, "")

QUICHE_FLAG(bool, quic_reloadable_flag_advertise_quic_for_https_for_external_users, false, "")

QUICHE_FLAG(bool, quic_reloadable_flag_quic_ack_delay_alarm_granularity, false,
            "When true, ensure the ACK delay is never less than the alarm granularity when ACK "
            "decimation is enabled.")

QUICHE_FLAG(bool, quic_reloadable_flag_quic_advance_ack_timeout_update, true,
            "If true, update ack timeout upon receiving an retransmittable frame.")

QUICHE_FLAG(bool, quic_reloadable_flag_quic_allow_backend_set_stream_ttl, false,
            "If true, check backend response header for X-Response-Ttl. If it is provided, the "
            "stream TTL is set. A QUIC stream will be immediately canceled when tries to write "
            "data if this TTL expired.")

QUICHE_FLAG(bool, quic_reloadable_flag_quic_allow_client_enabled_bbr_v2, true,
            "If true, allow client to enable BBRv2 on server via connection option 'B2ON'.")

QUICHE_FLAG(bool, quic_reloadable_flag_quic_alpn_dispatch, false,
            "Support different QUIC sessions, as indicated by ALPN. Used for QBONE.")

QUICHE_FLAG(bool, quic_reloadable_flag_quic_always_send_earliest_ack, false,
            "If true, SendAllPendingAcks always send the earliest ACK.")

QUICHE_FLAG(
    bool, quic_reloadable_flag_quic_avoid_leak_writer_buffer, false,
    "If true, QUIC will free writer-allocated packet buffer if writer->WritePacket is not called.")

QUICHE_FLAG(bool, quic_reloadable_flag_quic_bbr2_add_ack_height_to_queueing_threshold, true,
            "If true, QUIC BBRv2 to take ack height into account when calculating "
            "queuing_threshold in PROBE_UP.")

QUICHE_FLAG(bool, quic_reloadable_flag_quic_bbr2_avoid_too_low_probe_bw_cwnd, false,
            "If true, QUIC BBRv2's PROBE_BW mode will not reduce cwnd below BDP+ack_height.")

QUICHE_FLAG(bool, quic_reloadable_flag_quic_bbr2_fewer_startup_round_trips, false,
            "When true, the 1RTT and 2RTT connection options decrease the number of round trips in "
            "BBRv2 STARTUP without a 25% bandwidth increase to 1 or 2 round trips respectively.")

QUICHE_FLAG(bool, quic_reloadable_flag_quic_bbr2_ignore_inflight_lo, false,
            "When true, QUIC's BBRv2 ignores inflight_lo in PROBE_BW.")

QUICHE_FLAG(
    bool, quic_reloadable_flag_quic_bbr2_limit_inflight_hi, false,
    "When true, the B2HI connection option limits reduction of inflight_hi to (1-Beta)*CWND.")

QUICHE_FLAG(bool, quic_reloadable_flag_quic_bbr_default_exit_startup_on_loss, true,
            "If true, QUIC will enable connection options LRTT+BBQ2 by default.")

QUICHE_FLAG(bool, quic_reloadable_flag_quic_bbr_donot_inject_bandwidth, true,
            "If true, do not inject bandwidth in BbrSender::AdjustNetworkParameters.")

QUICHE_FLAG(bool, quic_reloadable_flag_quic_bbr_fix_pacing_rate, true,
            "If true, re-calculate pacing rate when cwnd gets bootstrapped.")

QUICHE_FLAG(bool, quic_reloadable_flag_quic_bbr_flexible_app_limited, false,
            "When true and the BBR9 connection option is present, BBR only considers bandwidth "
            "samples app-limited if they're not filling the pipe.")

QUICHE_FLAG(bool, quic_reloadable_flag_quic_bbr_mitigate_overly_large_bandwidth_sample, true,
            "If true, when cwnd gets bootstrapped and causing badly overshoot, reset cwnd and "
            "pacing rate based on measured bw.")

QUICHE_FLAG(bool, quic_reloadable_flag_quic_bbr_no_bytes_acked_in_startup_recovery, false,
            "When in STARTUP and recovery, do not add bytes_acked to QUIC BBR's CWND in "
            "CalculateCongestionWindow()")

QUICHE_FLAG(bool, quic_reloadable_flag_quic_bootstrap_cwnd_by_spdy_priority, true,
            "If true, bootstrap initial QUIC cwnd by SPDY priorities.")

QUICHE_FLAG(bool, quic_reloadable_flag_quic_bw_sampler_app_limited_starting_value, true,
            "If true, quic::BandwidthSampler will start in application limited phase.")

QUICHE_FLAG(bool, quic_reloadable_flag_quic_conservative_bursts, false,
            "If true, set burst token to 2 in cwnd bootstrapping experiment.")

QUICHE_FLAG(bool, quic_reloadable_flag_quic_conservative_cwnd_and_pacing_gains, false,
            "If true, uses conservative cwnd gain and pacing gain when cwnd gets bootstrapped.")

QUICHE_FLAG(bool, quic_reloadable_flag_quic_default_enable_5rto_blackhole_detection2, false,
            "If true, default-enable 5RTO blachole detection.")

QUICHE_FLAG(bool, quic_reloadable_flag_quic_default_on_pto, false,
            "If true, default on PTO which unifies TLP + RTO loss recovery.")

QUICHE_FLAG(bool, quic_reloadable_flag_quic_default_to_bbr, true,
            "When true, defaults to BBR congestion control instead of Cubic.")

QUICHE_FLAG(bool, quic_reloadable_flag_quic_default_to_bbr_v2, false,
            "If true, use BBRv2 as the default congestion controller. Takes precedence over "
            "--quic_default_to_bbr.")

QUICHE_FLAG(bool, quic_reloadable_flag_quic_disable_version_q043, false,
            "If true, disable QUIC version Q043.")

QUICHE_FLAG(bool, quic_reloadable_flag_quic_disable_version_q046, false,
            "If true, disable QUIC version Q046.")

QUICHE_FLAG(bool, quic_reloadable_flag_quic_disable_version_q048, false,
            "If true, disable QUIC version Q048.")

QUICHE_FLAG(bool, quic_reloadable_flag_quic_disable_version_q049, false,
            "If true, disable QUIC version Q049.")

QUICHE_FLAG(bool, quic_reloadable_flag_quic_disable_version_q050, false,
            "If true, disable QUIC version Q050.")

QUICHE_FLAG(bool, quic_reloadable_flag_quic_disable_version_t050, false,
            "If true, disable QUIC version h3-T050.")

QUICHE_FLAG(
    bool, quic_reloadable_flag_quic_do_not_accept_stop_waiting, false,
    "In v44 and above, where STOP_WAITING is never sent, close the connection if it's received.")

QUICHE_FLAG(bool, quic_reloadable_flag_quic_donot_change_queued_ack, false,
            "If true, do not change ACK in PostProcessAckFrame if an ACK has been queued.")

QUICHE_FLAG(bool, quic_reloadable_flag_quic_donot_reset_ideal_next_packet_send_time, false,
            "If true, stop resetting ideal_next_packet_send_time_ in pacing sender.")

QUICHE_FLAG(bool, quic_reloadable_flag_quic_enable_ack_decimation, true,
            "Default enables QUIC ack decimation and adds a connection option to disable it.")

QUICHE_FLAG(bool, quic_reloadable_flag_quic_enable_loss_detection_experiment_at_gfe, false,
            "If ture, enable GFE-picked loss detection experiment.")

QUICHE_FLAG(bool, quic_reloadable_flag_quic_enable_loss_detection_tuner, false,
            "If true, allow QUIC loss detection tuning to be enabled by connection option ELDT.")

QUICHE_FLAG(bool, quic_reloadable_flag_quic_enable_tls_resumption, false,
            "If true, enables support for TLS resumption in QUIC.")

QUICHE_FLAG(bool, quic_reloadable_flag_quic_enable_version_draft_25_v3, true,
            "If true, enable QUIC version h3-25.")

QUICHE_FLAG(bool, quic_reloadable_flag_quic_enable_version_draft_27, true,
            "If true, enable QUIC version h3-27.")

QUICHE_FLAG(bool, quic_reloadable_flag_quic_enable_version_draft_28, false,
            "If true, enable QUIC version h3-28.")

QUICHE_FLAG(bool, quic_reloadable_flag_quic_enable_zero_rtt_for_tls, false,
            "If true, support for IETF QUIC 0-rtt is enabled.")

QUICHE_FLAG(bool, quic_reloadable_flag_quic_enabled, false, "")

QUICHE_FLAG(bool, quic_reloadable_flag_quic_extend_idle_time_on_decryptable_packets, true,
            "If true, only extend idle time on decryptable packets.")

QUICHE_FLAG(bool, quic_reloadable_flag_quic_fix_bbr_cwnd_in_bandwidth_resumption, true,
            "If true, adjust congestion window when doing bandwidth resumption in BBR.")

QUICHE_FLAG(bool, quic_reloadable_flag_quic_fix_checking_should_generate_packet, false,
            "If true, check ShouldGeneratePacket for every crypto packet.")

QUICHE_FLAG(bool, quic_reloadable_flag_quic_fix_last_inflight_packets_sent_time, false,
            "If true, clear last_inflight_packets_sent_time_ of a packet number space when there "
            "is no bytes in flight.")

QUICHE_FLAG(bool, quic_reloadable_flag_quic_fix_server_pto_timeout, false,
            "If true, do not arm PTO on half RTT packets if they are the only ones in flight.")

QUICHE_FLAG(bool, quic_reloadable_flag_quic_fix_willing_and_able_to_write, false,
            "If true, check connection level flow control for send control stream and qpack "
            "streams in QuicSession::WillingAndAbleToWrite.")

QUICHE_FLAG(
    bool, quic_reloadable_flag_quic_fix_write_pending_crypto_retransmission, false,
    "If true, return from QuicCryptoStream::WritePendingCryptoRetransmission after partial writes.")

QUICHE_FLAG(bool, quic_reloadable_flag_quic_listener_never_fake_epollout, false,
            "If true, QuicListener::OnSocketIsWritable will always return false, which means there "
            "will never be a fake EPOLLOUT event in the next epoll iteration.")

QUICHE_FLAG(bool, quic_reloadable_flag_quic_move_amplification_limit, false,
            "When true, always check the amplification limit before writing, not just for "
            "handshake packets.")

QUICHE_FLAG(bool, quic_reloadable_flag_quic_negotiate_ack_delay_time, true,
            "If true, will negotiate the ACK delay time.")

QUICHE_FLAG(bool, quic_reloadable_flag_quic_no_dup_experiment_id_2, false,
            "If true, transport connection stats doesn't report duplicated experiments for same "
            "connection.")

QUICHE_FLAG(bool, quic_reloadable_flag_quic_notify_stream_id_manager_when_disconnected, false,
            "If true, notify stream ID manager even connection disconnects.")

QUICHE_FLAG(bool, quic_reloadable_flag_quic_only_truncate_long_cids, true,
            "In IETF QUIC, only truncate long CIDs from the client's Initial, don't modify them.")

QUICHE_FLAG(bool, quic_reloadable_flag_quic_proxy_write_packed_strings, false,
            "If true, QuicProxyDispatcher will write packed_client_address and packed_server_vip "
            "in TcpProxyHeaderProto.")

QUICHE_FLAG(bool, quic_reloadable_flag_quic_record_frontend_service_vip_mapping, true,
            "If true, for L1 GFE, as requests come in, record frontend service to VIP mapping "
            "which is used to announce VIP in SHLO for proxied sessions. ")

QUICHE_FLAG(bool, quic_reloadable_flag_quic_reject_all_traffic, false, "")

QUICHE_FLAG(bool, quic_reloadable_flag_quic_require_handshake_confirmation, false,
            "If true, require handshake confirmation for QUIC connections, functionally disabling "
            "0-rtt handshakes.")

QUICHE_FLAG(bool, quic_reloadable_flag_quic_send_timestamps, false,
            "When the STMP connection option is sent by the client, timestamps in the QUIC ACK "
            "frame are sent and processed.")

QUICHE_FLAG(bool, quic_reloadable_flag_quic_send_two_alt_addresses, true,
            "When true, GFE will send two AlternateServerAddress (IPv6+IPv4) instead of one.")

QUICHE_FLAG(bool, quic_reloadable_flag_quic_server_push, true,
            "If true, enable server push feature on QUIC.")

QUICHE_FLAG(bool, quic_reloadable_flag_quic_stop_sending_duplicate_max_streams, false,
            "If true, session does not send duplicate MAX_STREAMS.")

QUICHE_FLAG(bool, quic_reloadable_flag_quic_testonly_default_false, false,
            "A testonly reloadable flag that will always default to false.")

QUICHE_FLAG(bool, quic_reloadable_flag_quic_testonly_default_true, true,
            "A testonly reloadable flag that will always default to true.")

QUICHE_FLAG(bool, quic_reloadable_flag_quic_tls_enforce_valid_sni, false,
            "If true, reject IETF QUIC connections with invalid SNI.")

QUICHE_FLAG(bool, quic_reloadable_flag_quic_unified_iw_options, false,
            "When true, set the initial congestion control window from connection options in "
            "QuicSentPacketManager rather than TcpCubicSenderBytes.")

QUICHE_FLAG(bool, quic_reloadable_flag_quic_update_ack_alarm_in_send_all_pending_acks, false,
            "If true, QuicConnection::SendAllPendingAcks will Update instead of Set the ack alarm.")

QUICHE_FLAG(bool, quic_reloadable_flag_quic_use_dispatcher_clock_for_read_timestamp, false,
            "If true, in QuicListener, use QuicDispatcher's clock as the source for packet read "
            "timestamps.")

QUICHE_FLAG(bool, quic_reloadable_flag_quic_use_header_stage_idle_list2, false,
            "If true, use header stage idle list for QUIC connections in GFE.")

QUICHE_FLAG(
    bool, quic_reloadable_flag_quic_use_idle_network_detector, false,
    "If true, use idle network detector to detect handshake timeout and idle network timeout.")

QUICHE_FLAG(bool, quic_reloadable_flag_quic_use_ip_bandwidth_module, true,
            "If true, use IpBandwidthModule for cwnd bootstrapping if it is registered.")

QUICHE_FLAG(bool, quic_reloadable_flag_quic_use_leto_key_exchange, false,
            "If true, QUIC will attempt to use the Leto key exchange service and only fall back to "
            "local key exchange if that fails.")

QUICHE_FLAG(bool, quic_reloadable_flag_send_quic_fallback_server_config_on_leto_error, false,
            "If true and using Leto for QUIC shared-key calculations, GFE will react to a failure "
            "to contact Leto by sending a REJ containing a fallback ServerConfig, allowing the "
            "client to continue the handshake.")

QUICHE_FLAG(
    bool, quic_restart_flag_dont_fetch_quic_private_keys_from_leto, false,
    "If true, GFE will not request private keys when fetching QUIC ServerConfigs from Leto.")

QUICHE_FLAG(bool, quic_restart_flag_quic_adjust_initial_cwnd_by_gws, true,
            "If true, GFE informs backend that a client request is the first one on the connection "
            "via frontline header \"first_request=1\". Also, adjust initial cwnd based on "
            "X-Google-Gws-Initial-Cwnd-Mode sent by GWS.")

QUICHE_FLAG(
    bool, quic_restart_flag_quic_allow_loas_multipacket_chlo, false,
    "If true, inspects QUIC CHLOs for kLOAS and early creates sessions to allow multi-packet CHLOs")

QUICHE_FLAG(bool, quic_restart_flag_quic_google_transport_param_omit_old, false,
            "When true, QUIC+TLS will not send nor parse the old-format Google-specific transport "
            "parameters.")

QUICHE_FLAG(
    bool, quic_restart_flag_quic_google_transport_param_send_new, false,
    "When true, QUIC+TLS will send and parse the new-format Google-specific transport parameters.")

QUICHE_FLAG(bool, quic_restart_flag_quic_ignore_cid_first_byte_in_bpf, false,
            "If true, ignore CID first byte in BPF for both UDP socket and RX_RING.")

QUICHE_FLAG(bool, quic_restart_flag_quic_offload_pacing_to_usps2, false,
            "If true, QUIC offload pacing when using USPS as egress method.")

QUICHE_FLAG(bool, quic_restart_flag_quic_replace_gfe_connection_ids, false,
            "When true, GfeQuicDispatcher will replace long connection IDs with 64bit ones before "
            "inserting them in the connection map.")

QUICHE_FLAG(bool, quic_restart_flag_quic_replace_time_wait_list_encryption_level, false,
            "Replace the usage of ConnectionData::encryption_level in quic_time_wait_list_manager "
            "with a new TimeWaitAction.")

QUICHE_FLAG(bool, quic_restart_flag_quic_rx_ring_use_tpacket_v3, false,
            "If true, use TPACKET_V3 for QuicRxRing instead of TPACKET_V2.")

QUICHE_FLAG(bool, quic_restart_flag_quic_should_accept_new_connection, false,
            "If true, reject QUIC CHLO packets when dispatcher is asked to do so.")

QUICHE_FLAG(bool, quic_restart_flag_quic_support_release_time_for_gso, false,
            "If true, QuicGsoBatchWriter will support release time if it is available and the "
            "process has the permission to do so.")

QUICHE_FLAG(bool, quic_restart_flag_quic_testonly_default_false, false,
            "A testonly restart flag that will always default to false.")

QUICHE_FLAG(bool, quic_restart_flag_quic_testonly_default_true, true,
            "A testonly restart flag that will always default to true.")

QUICHE_FLAG(
    bool, quic_restart_flag_quic_use_leto_for_quic_configs, false,
    "If true, use Leto to fetch QUIC server configs instead of using the seeds from Memento.")

QUICHE_FLAG(bool, quic_restart_flag_quic_use_pigeon_socket_to_backend, false,
            "If true, create a shared pigeon socket for all quic to backend connections and switch "
            "to use it after successful handshake.")

QUICHE_FLAG(bool, spdy_reloadable_flag_fix_spdy_header_coalescing, false,
            "If true, when coalescing multivalued spdy headers, only headers that exist in spdy "
            "headers block are updated.")

QUICHE_FLAG(bool, spdy_reloadable_flag_quic_bootstrap_cwnd_by_spdy_priority, true,
            "If true, bootstrap initial QUIC cwnd by SPDY priorities.")

QUICHE_FLAG(
    bool, spdy_reloadable_flag_spdy_discard_response_body_if_disallowed, false,
    "If true, SPDY will discard all response body bytes when response code indicates no response "
    "body should exist. Previously, we only discard partial bytes on the first response processing "
    "and the rest of the response bytes would still be delivered even though the response code "
    "said there should not be any body associated with the response code.")

QUICHE_FLAG(bool, quic_allow_chlo_buffering, true,
            "If true, allows packets to be buffered in anticipation of a "
            "future CHLO, and allow CHLO packets to be buffered until next "
            "iteration of the event loop.")

QUICHE_FLAG(bool, quic_disable_pacing_for_perf_tests, false, "If true, disable pacing in QUIC")

QUICHE_FLAG(bool, quic_enforce_single_packet_chlo, true,
            "If true, enforce that QUIC CHLOs fit in one packet")

QUICHE_FLAG(int64_t, quic_time_wait_list_max_connections, 600000,
            "Maximum number of connections on the time-wait list. A negative value implies no "
            "configured limit.")

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

QUICHE_FLAG(int32_t, quic_lumpy_pacing_size, 2,
            "Number of packets that the pacing sender allows in bursts during "
            "pacing. This flag is ignored if a flow's estimated bandwidth is "
            "lower than 1200 kbps.")

QUICHE_FLAG(double, quic_lumpy_pacing_cwnd_fraction, 0.25f,
            "Congestion window fraction that the pacing sender allows in bursts "
            "during pacing.")

QUICHE_FLAG(int32_t, quic_max_pace_time_into_future_ms, 10,
            "Max time that QUIC can pace packets into the future in ms.")

QUICHE_FLAG(double, quic_pace_time_into_future_srtt_fraction, 0.125f,
            "Smoothed RTT fraction that a connection can pace packets into the future.")

QUICHE_FLAG(bool, quic_export_server_num_packets_per_write_histogram, false,
            "If true, export number of packets written per write operation histogram.")

QUICHE_FLAG(bool, quic_disable_version_negotiation_grease_randomness, false,
            "If true, use predictable version negotiation versions.")

QUICHE_FLAG(bool, quic_enable_http3_grease_randomness, true,
            "If true, use random greased settings and frames.")

QUICHE_FLAG(int64_t, quic_max_tracked_packet_count, 10000, "Maximum number of tracked packets.")

QUICHE_FLAG(bool, quic_prober_uses_length_prefixed_connection_ids, false,
            "If true, QuicFramer::WriteClientVersionNegotiationProbePacket uses "
            "length-prefixed connection IDs.")

QUICHE_FLAG(bool, quic_client_convert_http_header_name_to_lowercase, true,
            "If true, HTTP request header names sent from QuicSpdyClientBase(and "
            "descendents) will be automatically converted to lower case.")

QUICHE_FLAG(bool, quic_enable_http3_server_push, false,
            "If true, server push will be allowed in QUIC versions that use HTTP/3.")

QUICHE_FLAG(int32_t, quic_bbr2_default_probe_bw_base_duration_ms, 2000,
            "The default minimum duration for BBRv2-native probes, in milliseconds.")

QUICHE_FLAG(int32_t, quic_bbr2_default_probe_bw_max_rand_duration_ms, 1000,
            "The default upper bound of the random amount of BBRv2-native "
            "probes, in milliseconds.")

QUICHE_FLAG(int32_t, quic_bbr2_default_probe_rtt_period_ms, 10000,
            "The default period for entering PROBE_RTT, in milliseconds.")

QUICHE_FLAG(double, quic_bbr2_default_loss_threshold, 0.02,
            "The default loss threshold for QUIC BBRv2, should be a value "
            "between 0 and 1.")

QUICHE_FLAG(int32_t, quic_bbr2_default_startup_full_loss_count, 8,
            "The default minimum number of loss marking events to exit STARTUP.")

QUICHE_FLAG(int32_t, quic_bbr2_default_probe_bw_full_loss_count, 2,
            "The default minimum number of loss marking events to exit PROBE_UP phase.")

QUICHE_FLAG(double, quic_bbr2_default_inflight_hi_headroom, 0.01,
            "The default fraction of unutilized headroom to try to leave in path "
            "upon high loss.")

QUICHE_FLAG(int32_t, quic_bbr2_default_initial_ack_height_filter_window, 10,
            "The default initial value of the max ack height filter's window length.")

QUICHE_FLAG(double, quic_ack_aggregation_bandwidth_threshold, 1.0,
            "If the bandwidth during ack aggregation is smaller than (estimated "
            "bandwidth * this flag), consider the current aggregation completed "
            "and starts a new one.")

QUICHE_FLAG(int32_t, quic_anti_amplification_factor, 3,
            "Anti-amplification factor. Before address validation, server will "
            "send no more than factor times bytes received.")

QUICHE_FLAG(int32_t, quic_max_buffered_crypto_bytes, 16 * 1024,
            "The maximum amount of CRYPTO frame data that can be buffered.")

QUICHE_FLAG(int32_t, quic_max_aggressive_retransmittable_on_wire_ping_count, 0,
            "If set to non-zero, the maximum number of consecutive pings that "
            "can be sent with aggressive initial retransmittable on wire timeout "
            "if there is no new data received. After which, the timeout will be "
            "exponentially back off until exceeds the default ping timeout.")

QUICHE_FLAG(int32_t, quic_max_congestion_window, 2000, "The maximum congestion window in packets.")

QUICHE_FLAG(int32_t, quic_max_streams_window_divisor, 2,
            "The divisor that controls how often MAX_STREAMS frame is sent.")

QUICHE_FLAG(bool, http2_reloadable_flag_http2_testonly_default_false, false,
            "A testonly reloadable flag that will always default to false.")

QUICHE_FLAG(bool, http2_restart_flag_http2_testonly_default_false, false,
            "A testonly restart flag that will always default to false.")

QUICHE_FLAG(bool, spdy_reloadable_flag_spdy_testonly_default_false, false,
            "A testonly reloadable flag that will always default to false.")

QUICHE_FLAG(bool, spdy_restart_flag_spdy_testonly_default_false, false,
            "A testonly restart flag that will always default to false.")

#endif
