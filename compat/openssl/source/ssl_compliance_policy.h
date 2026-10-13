#pragma once

#ifndef _SSL_COMPLIANCE_POLICY_H_
#define _SSL_COMPLIANCE_POLICY_H_

#include <openssl/ssl.h>

/*
 * OpenSSL has no accessor to read back a connection's compliance policy, so
 * SSL_set_compliance_policy() stores the requested policy in SSL ex_data and
 * SSL_get_compliance_policy() reads it back from there. Both use the shared
 * ex_data index returned by this function.
 */
int sslCompliancePolicyExDataIndex();

#endif /*_SSL_COMPLIANCE_POLICY_H_*/
