package org.cronvoy;

import org.chromium.net.CronetException;

/** Implements {@link CronetException}. */
final class CronetExceptionImpl extends CronetException {

  public CronetExceptionImpl(String message, Throwable cause) { super(message, cause); }
}
