// This include file is visible from just about every *.cpp file.
// It defines some specific-length integral types, and a few other commonly
// needed declarations.

#ifndef _INCLUDE_W2200_H_
#define _INCLUDE_W2200_H_

#include "compile_options.h"    // compile-time flags
#include <cassert>

#include <string>
using std::string;
#include <vector>
using std::vector;
#include <memory>
using std::shared_ptr;

class Timer;
using spTimer = std::shared_ptr<Timer>;

// types used globally in the wang emulator

#include <cstdint>
typedef uint64_t  uint64;
typedef  int64_t   int64;
typedef uint32_t  uint32;
typedef  int32_t   int32;
typedef uint16_t  uint16;
typedef  int16_t   int16;
typedef  uint8_t   uint8;
typedef   int8_t    int8;
typedef  uint8_t   uint4;  // for clarity; not really 4b


// Put this at the end of a class declaration to make the class
// uncopyable and unassignable

#define CANT_ASSIGN_CLASS(ClassName)                 \
  public:                                            \
    ClassName& operator=(const ClassName&) = delete;

#define CANT_ASSIGN_OR_COPY_CLASS(ClassName)         \
  public:                                            \
    ClassName& operator=(const ClassName&) = delete; \
    ClassName(ClassName&) = delete;

#endif  // _INCLUDE_W2200_H_

// vim: ts=8:et:sw=4:smarttab
