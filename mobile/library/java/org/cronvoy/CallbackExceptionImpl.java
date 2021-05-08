package org.cronvoy;

import org.chromium.net.CallbackException;

/** An implementation of {@link CallbackException}. */
final class CallbackExceptionImpl extends CallbackException {

  CallbackExceptionImpl(String message, Throwable cause) { super(message, cause); }
}
