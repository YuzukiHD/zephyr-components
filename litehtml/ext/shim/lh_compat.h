/*
 * Force included into every C++ file of the sub build.
 *
 * libstdc++ declares std::string and friends as explicitly instantiated in its binary
 * (extern templates), and that binary instantiation also holds the stream operators and
 * so drags the whole stream and exception runtime into the image. The macro is set by
 * c++config.h itself, so it is changed after that header.
 */
#ifdef __cplusplus
#include <bits/c++config.h>
#undef _GLIBCXX_EXTERN_TEMPLATE
#define _GLIBCXX_EXTERN_TEMPLATE 0

/* C99 math functions that this libstdc++ does not export into std */
#include <cmath>
namespace std {
using ::nearbyint;
}
#endif
