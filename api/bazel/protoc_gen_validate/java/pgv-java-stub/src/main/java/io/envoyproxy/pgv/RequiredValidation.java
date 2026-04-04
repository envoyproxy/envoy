package io.envoyproxy.pgv;

import com.google.protobuf.Message;

/**
 * {@code RequiredValidation} implements PGV validation for required fields.
 */
public final class RequiredValidation {
    private RequiredValidation() {
    }

    public static void required(String field, Message value) throws ValidationException {
        if (value == null) {
            throw new ValidationException(field, "null", "is required");
        }
    }
}
