#pragma once

namespace Lyft {
/**
 * @return the size of a C array.
 */
#define ARRAY_SIZE(X) (sizeof(X) / sizeof(X[0]))

/**
 * Helper macros from enum to string macros.
 */
#define GENERATE_ENUM(X) X,
#define GENERATE_STRING(X) #X,

/**
 * Stop the compiler from complaining about an unreferenced parameter.
 */
#define UNREFERENCED_PARAMETER(X) ((void)(X))
} // Lyft