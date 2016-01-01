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


// -------------------------------------------------------------------------
// Normally, we don't want any wx-stuff being visible from the non-UI code.
// However, if we enable leak checking in wxwidgets, it is nice to have it
// track objects allocated in the non-UI code too.
//
// Ideally I'll find a simple and more powerful debugging library that
// can be used instead.  Until then, we can expose the wx world to the
// core emulator.

// FIXME: this is error prone -- if any .cpp doesn't include this file
//        one way or another, it will escape this leak annotation.
#if GLOBAL_NEW_DEBUGGING
    #include "wx/wx.h"

    // Normally, new is automatically defined to be the debugging version.
    // If not, this does it.
    #if !defined(new) && defined(WXDEBUG_NEW) && wxUSE_MEMORY_TRACING && wxUSE_GLOBAL_MEMORY_OPERATORS
    #define new WXDEBUG_NEW
    #endif
#endif

#endif  // _INCLUDE_W2200_H_

// vim: ts=8:et:sw=4:smarttab
