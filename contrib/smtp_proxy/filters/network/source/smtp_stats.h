#pragma once
#include "envoy/stats/scope.h"
#include "envoy/stats/stats.h"
#include "envoy/stats/stats_macros.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace SmtpProxy {

/**
 * All SMTP proxy stats. @see stats_macros.h
 */
#define ALL_SMTP_PROXY_STATS(COUNTER, GUAGE, HISTOGRAM)                                            \
  COUNTER(session_requests)                                                                        \
  COUNTER(connection_establishment_errors)                                                         \
  COUNTER(session_completed)                                                                       \
  COUNTER(session_terminated)                                                                      \
  COUNTER(transaction_req)                                                                         \
  COUNTER(transaction_completed)                                                                   \
  COUNTER(transaction_failed)                                                                      \
  COUNTER(transaction_aborted)                                                                     \
  COUNTER(downstream_tls_termination_success)                                                      \
  COUNTER(downstream_tls_termination_error)                                                        \
  COUNTER(upstream_tls_success)                                                                    \
  COUNTER(upstream_tls_error)                                                                      \
  COUNTER(auth_errors)                                                                             \
  COUNTER(mail_data_transfer_errors)                                                               \
  COUNTER(rcpt_errors)                                                                             \
  COUNTER(upstream_4xx_errors)                                                                     \
  COUNTER(upstream_4xx_mail_from)                                                                  \
  COUNTER(upstream_4xx_rcpt)                                                                       \
  COUNTER(upstream_4xx_data)                                                                       \
  COUNTER(upstream_4xx_auth)                                                                       \
  COUNTER(local_4xx_errors)                                                                        \
  COUNTER(upstream_5xx_errors)                                                                     \
  COUNTER(upstream_5xx_ehlo)                                                                       \
  COUNTER(upstream_5xx_mail_from)                                                                  \
  COUNTER(upstream_5xx_rcpt)                                                                       \
  COUNTER(upstream_5xx_data)                                                                       \
  COUNTER(upstream_5xx_auth)                                                                       \
  COUNTER(local_5xx_errors)                                                                        \
  COUNTER(local_5xx_missing_ehlo)                                                                  \
  COUNTER(local_5xx_missing_mail)                                                                  \
  COUNTER(local_5xx_missing_rcpt)                                                                  \
  COUNTER(local_5xx_duplilcate_auth)                                                               \
  COUNTER(local_5xx_duplilcate_starttls)                                                           \
  COUNTER(passthrough_sessions)                                                                    \
  COUNTER(protocol_parse_error)                                                                    \
  COUNTER(bad_cmd_sequence)                                                                        \
  COUNTER(duplicate_cmd)                                                                           \
  COUNTER(mail_req_rejected_due_to_non_tls)                                                        \
  GUAGE(transaction_active, Accumulate)                                                            \
  GUAGE(session_active, Accumulate)                                                                \
  HISTOGRAM(transaction_length, Milliseconds)                                                      \
  HISTOGRAM(session_length, Milliseconds)                                                          \
  HISTOGRAM(data_tx_length, Milliseconds)                                                          \
  HISTOGRAM(command_length, Milliseconds)

/**
 * Struct definition for all SMTP proxy stats. @see stats_macros.h
 */
struct SmtpProxyStats {
  ALL_SMTP_PROXY_STATS(GENERATE_COUNTER_STRUCT, GENERATE_GAUGE_STRUCT, GENERATE_HISTOGRAM_STRUCT)
};

} // namespace SmtpProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
