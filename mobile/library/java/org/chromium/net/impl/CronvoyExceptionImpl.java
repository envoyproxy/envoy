package org.chromium.net.impl;

import org.chromium.net.CronetException;

/** Implements {@link CronetException}. */
final class CronvoyExceptionImpl extends CronetException {

  CronvoyExceptionImpl(String message, Throwable cause) { super(message, cause); }
}
