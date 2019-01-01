// This include file is visible from just about every *.cpp file.
// It defines some specific-length integral types, and a few other commonly
// needed declarations.

#ifndef _INCLUDE_W2200_H_
#define _INCLUDE_W2200_H_

#include "compile_options.h"    // compile-time flags

#include <array>
#include <cassert>
#include <functional>
#include <memory>
#include <string>
#include <vector>

// types used globally in the wang emulator

#include <cstdint>
using uint64 = uint64_t;
using  int64 =  int64_t;
using uint32 = uint32_t;
using  int32 =  int32_t;
using uint16 = uint16_t;
using  int16 =  int16_t;
using  uint8 =  uint8_t;
using   int8 =   int8_t;
using  uint4 =  uint8_t;  // for clarity; not really 4b

// Put this at the end of a class declaration to make the class
// uncopyable and unassignable

#define CANT_ASSIGN_CLASS(ClassName)                 \
  public:                                            \
    ClassName& operator=(const ClassName&) = delete;

#define CANT_ASSIGN_OR_COPY_CLASS(ClassName)         \
  public:                                            \
    ClassName& operator=(const ClassName&) = delete; \
    ClassName(ClassName&) = delete;

#endif // _INCLUDE_W2200_H_

// vim: ts=8:et:sw=4:smarttab
