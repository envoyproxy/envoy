package com.android.tools.r8.annotations;

import java.lang.annotation.ElementType;
import java.lang.annotation.Retention;
import java.lang.annotation.RetentionPolicy;
import java.lang.annotation.Target;

/**
 * Stub of the marker annotation D8/R8 emits on desugared lambdas. Only exists so
 * aar_import's ImportDepsChecker can resolve it; never linked into the APK.
 */
@Retention(RetentionPolicy.CLASS)
@Target({ElementType.METHOD, ElementType.TYPE})
public @interface LambdaMethod {}
