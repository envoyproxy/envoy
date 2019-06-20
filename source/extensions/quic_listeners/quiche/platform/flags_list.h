// This file intentionally does not have header guards, it's intended to be
// included multiple times, each time with a different definition of QUIC_FLAG.

// NOLINT(namespace-envoy)

// This file is part of the QUICHE platform implementation, and is not to be
// consumed or referenced directly by other Envoy code. It serves purely as a
// porting layer for QUICHE.

// The contents of this file are based off of
// //third_party/quic/core:quic_flags_list in google3, with the addition of
// testonly flags for testing http2 and spdy flags APIs.
// TODO(mpwarres): include generated flags_list.h as part of QUICHE.

#if defined(QUICHE_FLAG)

QUICHE_FLAG(bool, quic_reloadable_flag_advertise_quic_for_https_for_debugips, false, "")

QUICHE_FLAG(bool, quic_reloadable_flag_advertise_quic_for_https_for_external_users, false, "")

QUICHE_FLAG(bool, quic_reloadable_flag_enable_quic_stateless_reject_support, true,
            "Enables server-side support for QUIC stateless rejects.")

QUICHE_FLAG(bool, quic_reloadable_flag_quic_allow_backend_set_stream_ttl, false,
            "If true, check backend response header for X-Response-Ttl. If it is "
            "provided, the stream TTL is set. A QUIC stream will be immediately "
            "canceled when tries to write data if this TTL expired.")

QUICHE_FLAG(bool, quic_reloadable_flag_quic_alpn_dispatch, false,
            "Support different QUIC sessions, as indicated by ALPN. Used for QBONE.")

QUICHE_FLAG(bool, quic_reloadable_flag_quic_always_reset_short_header_packets, true,
            "If true, instead of send encryption none termination packets, send "
            "stateless reset in response to short headers.")

QUICHE_FLAG(bool, quic_reloadable_flag_quic_bbr_app_limited_recovery, false,
            "When you're app-limited entering recovery, stay app-limited until "
            "you exit recovery in QUIC BBR.")

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

QUICHE_FLAG(bool, quic_reloadable_flag_quic_debug_wrong_qos, false,
            "If true, consider getting QoS after stream has been detached as GFE bug.")

QUICHE_FLAG(bool, quic_reloadable_flag_quic_default_to_bbr, true,
            "When true, defaults to BBR congestion control instead of Cubic.")

QUICHE_FLAG(bool, quic_reloadable_flag_quic_deprecate_ack_bundling_mode, false,
            "If true, stop using AckBundling mode to send ACK, also deprecate "
            "ack_queued from QuicConnection.")

QUICHE_FLAG(bool, quic_reloadable_flag_quic_disable_connection_migration_for_udp_proxying, true,
            "If true, GFE disables connection migration in connection option for "
            "proxied sessions.")

QUICHE_FLAG(bool, quic_reloadable_flag_quic_disable_version_39, false,
            "If true, disable QUIC version 39.")

QUICHE_FLAG(bool, quic_reloadable_flag_quic_donot_reset_ideal_next_packet_send_time, false,
            "If true, stop resetting ideal_next_packet_send_time_ in pacing sender.")

QUICHE_FLAG(bool, quic_reloadable_flag_quic_eighth_rtt_loss_detection, false,
            "When true, the LOSS connection option allows for 1/8 RTT of "
            "reording instead of the current 1/8th threshold which has been "
            "found to be too large for fast loss recovery.")

QUICHE_FLAG(bool, quic_reloadable_flag_quic_enable_ack_decimation, false,
            "Default enables QUIC ack decimation and adds a connection option to "
            "disable it.")

QUICHE_FLAG(bool, quic_reloadable_flag_quic_enable_pcc3, false,
            "If true, enable experiment for testing PCC congestion-control.")

QUICHE_FLAG(bool, quic_reloadable_flag_quic_enable_version_43, true,
            "If true, enable QUIC version 43.")

QUICHE_FLAG(bool, quic_reloadable_flag_quic_enable_version_44, true,
            "If true, enable version 44 which uses IETF header format.")

QUICHE_FLAG(bool, quic_reloadable_flag_quic_enable_version_46, true,
            "If true, enable QUIC version 46.")

QUICHE_FLAG(bool, quic_reloadable_flag_quic_enable_version_47, false,
            "If true, enable QUIC version 47 which adds support for variable "
            "length connection IDs.")

QUICHE_FLAG(bool, quic_reloadable_flag_quic_enable_version_99, false, "If true, enable version 99.")

QUICHE_FLAG(bool, quic_reloadable_flag_quic_enabled, false, "")

QUICHE_FLAG(bool, quic_reloadable_flag_quic_faster_interval_add_in_sequence_buffer, false,
            "If true, QuicStreamSequencerBuffer will switch to a new "
            "QuicIntervalSet::AddOptimizedForAppend method in OnStreamData().")

QUICHE_FLAG(bool, quic_reloadable_flag_quic_fix_adaptive_time_loss, false,
            "Simplify QUIC's adaptive time loss detection to measure the "
            "necessary reordering window for every spurious retransmit.")

QUICHE_FLAG(bool, quic_reloadable_flag_quic_fix_has_pending_crypto_data, false,
            "If true, QuicSession::HasPendingCryptoData checks whether the "
            "crypto stream's send buffer is empty. This flag fixes a bug where "
            "the retransmission alarm mode is wrong for the first CHLO packet.")

QUICHE_FLAG(bool, quic_reloadable_flag_quic_fix_spurious_ack_alarm, false,
            "If true, do not schedule ack alarm if should_send_ack is set in the "
            "generator.")

QUICHE_FLAG(bool, quic_reloadable_flag_quic_fix_termination_packets, false,
            "If true, GFE time wait list will send termination packets based on "
            "current packet's encryption level.")

QUICHE_FLAG(bool, quic_reloadable_flag_quic_limit_window_updates_in_traces, false,
            "Limits the number of window update events recorded in Tracegraf logs.")

QUICHE_FLAG(bool, quic_reloadable_flag_quic_listener_never_fake_epollout, false,
            "If true, QuicListener::OnSocketIsWritable will always return false, "
            "which means there will never be a fake EPOLLOUT event in the next "
            "epoll iteration.")

QUICHE_FLAG(bool, quic_reloadable_flag_quic_log_cert_name_for_empty_sct, true,
            "If true, log leaf cert subject name into warning log.")

QUICHE_FLAG(bool, quic_reloadable_flag_quic_log_is_proxy_in_tcs, false,
            "If true, log whether a GFE QUIC server session is UDP proxied and whether "
            "it is a health check connection, in transport connection stats.")

QUICHE_FLAG(bool, quic_reloadable_flag_quic_logging_frames_in_tracegraf, false,
            "If true, populate frames info when logging tracegraf.")

QUICHE_FLAG(bool, quic_reloadable_flag_quic_monotonic_epoll_clock, false,
            "If true, QuicEpollClock::Now() will monotonically increase.")

QUICHE_FLAG(bool, quic_reloadable_flag_quic_no_client_conn_ver_negotiation, false,
            "If true, a client connection would be closed when a version "
            "negotiation packet is received. It would be the higher layer's "
            "responsibility to do the reconnection.")

QUICHE_FLAG(bool, quic_reloadable_flag_quic_no_cloud_domain_sni_lookup_on_missing_sni, false,
            "Do not attempt to match an empty Server Name Indication (SNI) "
            "against names extracted from Cloud customer certificates.")

QUICHE_FLAG(bool, quic_reloadable_flag_quic_no_dup_experiment_id_2, false,
            "If true, transport connection stats doesn't report duplicated "
            "experiments for same connection.")

QUICHE_FLAG(bool, quic_reloadable_flag_quic_no_goaway_for_proxied_port_change, false,
            "If true, for proxied quic sessions, GFE will not send a GOAWAY in "
            "response to a client port change.")

QUICHE_FLAG(bool, quic_reloadable_flag_quic_no_v2_scaling_factor, false,
            "When true, don't use an extra scaling factor when reading packets "
            "from QUIC's RX_RING with TPACKET_V2.")

QUICHE_FLAG(bool, quic_reloadable_flag_quic_optimize_inflight_check, false,
            "Stop checking QuicUnackedPacketMap::HasUnackedRetransmittableFrames "
            "and instead rely on the existing check that bytes_in_flight > 0")

QUICHE_FLAG(bool, quic_reloadable_flag_quic_proxy_check_toss_on_insertion_failure, false,
            "If true, enable the code that fixes a race condition for quic udp "
            "proxying in L0. See b/70036019.")

QUICHE_FLAG(bool, quic_reloadable_flag_quic_proxy_munge_response_for_healthcheck, true,
            "If true, for udp proxy, the health check packets from L1 to L0 will "
            "be munged.")

QUICHE_FLAG(bool, quic_reloadable_flag_quic_proxy_read_packed_strings, true,
            "If true, QuicProxyDispatcher will prefer to extract client_address "
            "and server_vip from packed_client_address and packed_server_vip, "
            "respectively.")

QUICHE_FLAG(bool, quic_reloadable_flag_quic_proxy_write_packed_strings, false,
            "If true, QuicProxyDispatcher will write packed_client_address and "
            "packed_server_vip in TcpProxyHeaderProto.")

QUICHE_FLAG(bool, quic_reloadable_flag_quic_record_frontend_service_vip_mapping, false,
            "If true, for L1 GFE, as requests come in, record frontend service to VIP "
            "mapping which is used to announce VIP in SHLO for proxied sessions. ")

QUICHE_FLAG(bool, quic_reloadable_flag_quic_reject_all_traffic, false, "")

QUICHE_FLAG(bool, quic_reloadable_flag_quic_require_handshake_confirmation, false,
            "If true, require handshake confirmation for QUIC connections, "
            "functionally disabling 0-rtt handshakes.")

QUICHE_FLAG(bool, quic_reloadable_flag_quic_rpm_decides_when_to_send_acks, false,
            "If both this flag and "
            "gfe2_reloadable_flag_quic_deprecate_ack_bundling_mode are true, "
            "QuicReceivedPacketManager decides when to send ACKs.")

QUICHE_FLAG(bool, quic_reloadable_flag_quic_send_timestamps, false,
            "When the STMP connection option is sent by the client, timestamps "
            "in the QUIC ACK frame are sent and processed.")

QUICHE_FLAG(bool, quic_reloadable_flag_quic_server_push, true,
            "If true, enable server push feature on QUIC.")

QUICHE_FLAG(bool, quic_reloadable_flag_quic_set_transmission_type_for_next_frame, true,
            "If true, QuicPacketCreator::SetTransmissionType will set the "
            "transmission type of the next successfully added frame.")

QUICHE_FLAG(bool, quic_reloadable_flag_quic_simplify_build_connectivity_probing_packet, true,
            "If true, simplifies the implementation of "
            "QuicFramer::BuildConnectivityProbingPacket().")

QUICHE_FLAG(bool, quic_reloadable_flag_quic_stateless_proxy, false,
            "If true, UDP proxy will not drop versionless packets, in other "
            "words, it will proxy all packets from client.")

QUICHE_FLAG(bool, quic_reloadable_flag_quic_stop_reading_when_level_triggered, false,
            "When true, calling StopReading() on a level-triggered QUIC stream "
            "sequencer will cause the sequencer to discard future data.")

QUICHE_FLAG(bool, quic_reloadable_flag_quic_testonly_default_false, false,
            "A testonly reloadable flag that will always default to false.")

QUICHE_FLAG(bool, quic_reloadable_flag_quic_testonly_default_true, true,
            "A testonly reloadable flag that will always default to true.")

QUICHE_FLAG(bool, quic_reloadable_flag_quic_tolerate_reneging, false,
            "If true, do not close connection if received largest acked decreases.")

QUICHE_FLAG(bool, quic_reloadable_flag_quic_unified_iw_options, false,
            "When true, set the initial congestion control window from connection "
            "options in QuicSentPacketManager rather than TcpCubicSenderBytes.")

QUICHE_FLAG(bool, quic_reloadable_flag_quic_use_cheap_stateless_rejects, true,
            "If true, QUIC will use cheap stateless rejects without creating a full "
            "connection. Prerequisite: --quic_allow_chlo_buffering has to be true.")

QUICHE_FLAG(bool, quic_reloadable_flag_quic_use_common_stream_check, false,
            "If true, use common code for checking whether a new stream ID may "
            "be allocated.")

QUICHE_FLAG(bool, quic_reloadable_flag_quic_use_header_stage_idle_list2, false,
            "If true, use header stage idle list for QUIC connections in GFE.")

QUICHE_FLAG(bool, quic_reloadable_flag_quic_use_leto_key_exchange, false,
            "If true, QUIC will attempt to use the Leto key exchange service and "
            "only fall back to local key exchange if that fails.")

QUICHE_FLAG(bool, quic_reloadable_flag_quic_use_new_append_connection_id, false,
            "When true QuicFramer will use AppendIetfConnectionIdsNew instead of "
            "AppendIetfConnectionId.")

QUICHE_FLAG(bool, quic_reloadable_flag_quic_use_pigeon_sockets, false,
            "Use USPS Direct Path for QUIC egress.")

QUICHE_FLAG(bool, quic_reloadable_flag_quic_use_quic_time_for_received_timestamp, false,
            "If true, use QuicClock::Now() for the fallback source of packet "
            "received time instead of WallNow().")

QUICHE_FLAG(bool, quic_reloadable_flag_quic_use_uber_loss_algorithm, false,
            "If true, use one loss algorithm per encryption level.")

QUICHE_FLAG(bool, quic_reloadable_flag_quic_use_uber_received_packet_manager, false,
            "If this flag and quic_rpm_decides_when_to_send_acks is true, use uber "
            "received packet manager instead of the single received packet manager.")

QUICHE_FLAG(bool, quic_reloadable_flag_quic_validate_packet_number_post_decryption, false,
            "If true, a QUIC endpoint will valid a received packet number after "
            "successfully decrypting the packet.")

QUICHE_FLAG(bool, quic_reloadable_flag_quic_eliminate_static_stream_map_3, false,
            "If true, static streams in a QuicSession will be stored inside dynamic stream map. "
            "static_stream_map will no longer be used.")

QUICHE_FLAG(bool, quic_reloadable_flag_quic_simplify_stop_waiting, false,
            "Do not send STOP_WAITING if no_stop_waiting_frame_ is true.")

QUICHE_FLAG(bool, quic_reloadable_flag_send_quic_fallback_server_config_on_leto_error, false,
            "If true and using Leto for QUIC shared-key calculations, GFE will react to a failure "
            "to contact Leto by sending a REJ containing a fallback ServerConfig, allowing the "
            "client to continue the handshake.")

QUICHE_FLAG(bool, quic_reloadable_flag_quic_fix_bbr_cwnd_in_bandwidth_resumption, true,
            " If true, adjust congestion window when doing bandwidth resumption in BBR.")

QUICHE_FLAG(bool, quic_reloadable_flag_quic_conservative_cwnd_and_pacing_gains, false,
            "If true, uses conservative cwnd gain and pacing gain.")

QUICHE_FLAG(
    bool, quic_reloadable_flag_quic_do_not_accept_stop_waiting, false,
    "In v44 and above, where STOP_WAITING is never sent, close the connection if it's received.")

QUICHE_FLAG(bool, quic_reloadable_flag_quic_loss_removes_from_inflight, false,
            "When true, remove packets from inflight where they're declared lost, rather than in "
            "MarkForRetransmission. Also no longer marks handshake packets as no longer inflight "
            "when they're retransmitted.")

QUICHE_FLAG(bool, quic_reloadable_flag_quic_conservative_bursts, false,
            "If true, set burst token to 2 in cwnd bootstrapping experiment.")

QUICHE_FLAG(bool, quic_reloadable_flag_quic_deprecate_queued_control_frames, false,
            "If true, deprecate queued_control_frames_ from QuicPacketGenerator.")

QUICHE_FLAG(bool, quic_reloadable_flag_quic_check_connected_before_flush, false,
            "If true, check whether connection is connected before flush.")

QUICHE_FLAG(bool, quic_reloadable_flag_quic_ignore_tlpr_if_sending_ping, false,
            "If true, ignore TLPR for retransmission delay when sending pings from ping alarm.")

QUICHE_FLAG(bool, quic_reloadable_flag_quic_terminate_gquic_connection_as_ietf, false,
            "If true, terminate Google QUIC connections similarly as IETF QUIC.")

QUICHE_FLAG(bool, quic_reloadable_flag_quic_disable_version_44, false,
            "If true, disable QUIC version 44.")

QUICHE_FLAG(
    bool, quic_reloadable_flag_quic_fix_packets_acked, false,
    "If true, when detecting losses, use packets_acked of corresponding packet number space.")

QUICHE_FLAG(bool, quic_reloadable_flag_quic_ignore_tlpr_if_no_pending_stream_data, false,
            "If true, ignore TLPR if there is no pending stream data")

QUICHE_FLAG(
    bool, quic_reloadable_flag_quic_drop_invalid_small_initial_connection_id, false,
    "When true, QuicDispatcher will drop packets that have an initial destination connection ID "
    "that is too short, instead of responding with a Version Negotiation packet to reject it.")

QUICHE_FLAG(bool, quic_restart_flag_quic_allow_loas_multipacket_chlo, false,
            "If true, inspects QUIC CHLOs for kLOAS and early creates sessions "
            "to allow multi-packet CHLOs")

QUICHE_FLAG(bool, quic_restart_flag_quic_enable_gso_for_udp_egress, false,
            "If 1) flag is true, 2) UDP egress_method is used in GFE config, and "
            "3) UDP GSO is supported by the kernel, GFE will use UDP GSO for "
            "egress, except for UDP proxy.")

QUICHE_FLAG(bool, quic_restart_flag_quic_enable_sendmmsg_for_udp_egress, false,
            "If 1) flag is true, 2) UDP egress_method is used in GFE config, and "
            "3) --gfe2_restart_flag_quic_enable_gso_for_udp_egress is false OR "
            "gso is not supported by kernel, GFE will use sendmmsg for egress, "
            "except for UDP proxy.")

QUICHE_FLAG(bool, quic_restart_flag_quic_no_server_conn_ver_negotiation2, false,
            "If true, dispatcher passes in a single version when creating a server "
            "connection, such that version negotiation is not supported in connection.")

QUICHE_FLAG(bool, quic_restart_flag_quic_offload_pacing_to_usps2, false,
            "If true, QUIC offload pacing when using USPS as egress method.")

QUICHE_FLAG(bool, quic_restart_flag_quic_pigeon_use_memfd_packet_memory, false,
            "If true, GFE QUIC will forcefully use memfd to create packet memory "
            "for pigeon socket. Otherwise memfd is used if "
            "--pigeon_sealable_files_enabled is true.")

QUICHE_FLAG(bool, quic_restart_flag_quic_rx_ring_use_tpacket_v3, false,
            "If true, use TPACKET_V3 for QuicRxRing instead of TPACKET_V2.")

QUICHE_FLAG(bool, quic_restart_flag_quic_server_handle_egress_epoll_err, false,
            "If true, handle EPOLLERRs from QUIC server egress sockets.")

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

QUICHE_FLAG(bool, quic_restart_flag_quic_do_not_override_connection_id, false,
            " When true, QuicFramer will not override connection IDs in headers and will instead "
            "respect the source/destination direction as expected by IETF QUIC.")

QUICHE_FLAG(bool, quic_restart_flag_quic_no_framer_object_in_dispatcher, false,
            "If true, make QuicDispatcher no longer have an instance of QuicFramer.")

QUICHE_FLAG(
    bool, quic_restart_flag_dont_fetch_quic_private_keys_from_leto, false,
    "If true, GFE will not request private keys when fetching QUIC ServerConfigs from Leto.")

QUICHE_FLAG(bool, quic_restart_flag_quic_use_allocated_connection_ids, false,
            "When true, QuicConnectionId will allocate long connection IDs on the heap instead of "
            "inline in the object.")

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
QUICHE_FLAG(int64_t, quic_time_wait_list_max_connections, 600000,
            "Maximum number of connections on the time-wait list. "
            "A negative value implies no configured limit.")

QUICHE_FLAG(int64_t, quic_time_wait_list_seconds, 200,
            "Time period for which a given connection_id should live in "
            "the time-wait state.")

QUICHE_FLAG(double, quic_bbr_cwnd_gain, 2.0f,
            "Congestion window gain for QUIC BBR during PROBE_BW phase.")

QUICHE_FLAG(int32_t, quic_buffered_data_threshold, 8 * 1024,
            "If buffered data in QUIC stream is less than this "
            "threshold, buffers all provided data or asks upper layer for more data")

QUICHE_FLAG(int32_t, quic_ietf_draft_version, 0,
            "Mechanism to override version label and ALPN for IETF interop.")

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

QUICHE_FLAG(double, quic_pace_time_into_future_srtt_fraction,
            0.125f, // One-eighth smoothed RTT
            "Smoothed RTT fraction that a connection can pace packets into the future.")

QUICHE_FLAG(bool, quic_export_server_num_packets_per_write_histogram, false,
            "If true, export number of packets written per write operation histogram.")

QUICHE_FLAG(int64_t, quic_headers_stream_id_in_v99, 0,
            "QUIC version 99 will use this stream ID for the headers stream.")

QUICHE_FLAG(bool, http2_reloadable_flag_http2_testonly_default_false, false,
            "A testonly reloadable flag that will always default to false.")

QUICHE_FLAG(bool, http2_restart_flag_http2_testonly_default_false, false,
            "A testonly restart flag that will always default to false.")

QUICHE_FLAG(bool, spdy_reloadable_flag_spdy_testonly_default_false, false,
            "A testonly reloadable flag that will always default to false.")

QUICHE_FLAG(bool, spdy_restart_flag_spdy_testonly_default_false, false,
            "A testonly restart flag that will always default to false.")

#endif
