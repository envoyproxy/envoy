/*
 * Copyright (C) 2022 Intel Corporation
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing,
 * software distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions
 * and limitations under the License.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include <openssl/ssl.h>


extern "C" const char *SSL_error_description(int err) {
  switch (err) {
#ifdef SSL_ERROR_NONE
    case SSL_ERROR_NONE: return "NONE";
#endif
#ifdef SSL_ERROR_SSL
    case SSL_ERROR_SSL: return "SSL";
#endif
#ifdef SSL_ERROR_WANT_READ
    case SSL_ERROR_WANT_READ: return "WANT_READ";
#endif
#ifdef SSL_ERROR_WANT_WRITE
    case SSL_ERROR_WANT_WRITE: return "WANT_WRITE";
#endif
#ifdef SSL_ERROR_WANT_X509_LOOKUP
    case SSL_ERROR_WANT_X509_LOOKUP: return "WANT_X509_LOOKUP";
#endif
#ifdef SSL_ERROR_SYSCALL
    case SSL_ERROR_SYSCALL: return "SYSCALL";
#endif
#ifdef SSL_ERROR_ZERO_RETURN
    case SSL_ERROR_ZERO_RETURN: return "ZERO_RETURN";
#endif
#ifdef SSL_ERROR_WANT_CONNECT
    case SSL_ERROR_WANT_CONNECT: return "WANT_CONNECT";
#endif
#ifdef SSL_ERROR_WANT_ACCEPT
    case SSL_ERROR_WANT_ACCEPT: return "WANT_ACCEPT";
#endif
#ifdef SSL_ERROR_PENDING_SESSION
    case SSL_ERROR_PENDING_SESSION: return "PENDING_SESSION";
#endif
#ifdef SSL_ERROR_PENDING_CERTIFICATE
    case SSL_ERROR_PENDING_CERTIFICATE: return "PENDING_CERTIFICATE";
#endif
#ifdef SSL_ERROR_WANT_PRIVATE_KEY_OPERATION
    case SSL_ERROR_WANT_PRIVATE_KEY_OPERATION: return "WANT_PRIVATE_KEY_OPERATION";
#endif
#ifdef SSL_ERROR_PENDING_TICKET
    case SSL_ERROR_PENDING_TICKET: return "PENDING_TICKET";
#endif
#ifdef SSL_ERROR_EARLY_DATA_REJECTED
    case SSL_ERROR_EARLY_DATA_REJECTED: return "EARLY_DATA_REJECTED";
#endif
#ifdef SSL_ERROR_WANT_CERTIFICATE_VERIFY
    case SSL_ERROR_WANT_CERTIFICATE_VERIFY: return "WANT_CERTIFICATE_VERIFY";
#endif
#ifdef SSL_ERROR_HANDOFF
    case SSL_ERROR_HANDOFF: return "HANDOFF";
#endif
#ifdef SSL_ERROR_HANDBACK
    case SSL_ERROR_HANDBACK: return "HANDBACK";
#endif
#ifdef SSL_ERROR_WANT_RENEGOTIATE
    case SSL_ERROR_WANT_RENEGOTIATE: return "WANT_RENEGOTIATE";
#endif
#ifdef SSL_ERROR_HANDSHAKE_HINTS_READY
    case SSL_ERROR_HANDSHAKE_HINTS_READY: return "HANDSHAKE_HINTS_READY";
#endif
    default:
      return NULL;
  }
}
