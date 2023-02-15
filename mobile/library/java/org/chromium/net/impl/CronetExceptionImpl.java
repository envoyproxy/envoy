package org.chromium.net.impl;

import org.chromium.net.CronetException;

/** Implements {@link CronetException}. */
final class CronetExceptionImpl extends CronetException {

  CronetExceptionImpl(String message, Throwable cause) { super(message, cause); }
}
