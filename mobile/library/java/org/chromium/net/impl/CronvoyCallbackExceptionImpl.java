package org.chromium.net.impl;

import org.chromium.net.CallbackException;

/** An implementation of {@link CallbackException}. */
public final class CronvoyCallbackExceptionImpl extends CallbackException {
  CronvoyCallbackExceptionImpl(String message, Throwable cause) { super(message, cause); }
}
