#ifndef _INCLUDE_COMPILE_OPTIONS_H_
#define _INCLUDE_COMPILE_OPTIONS_H_

// define these switches below to turn various features on and off.
// the various source files may have other switches but those tend to
// be not robust or half-baked experiments.  these should be fairly
// safe to enable or disable.

// ========================================================================
//  UiCrtFrame.cpp options
// ========================================================================

// define to 1 to have a menu option to dump the memory state to a file
// (also used in cpu_2200t and cpu_2200vp)
#define HAVE_FILE_DUMP 0

// define to 0 to have small toolbar buttons and 1 to have large buttons.
// the large buttons include the edit mode labels as well as SF key #.
#define BIG_BUTTONS 1

// if BIG_BUTTONS==1, then the following determines whether the EDIT mode
// labels are drawn with text (GRAPHIC_ARROWS==0) or lines (GRAPHIC_ARROWS==1).
#define GRAPHIC_ARROWS 1

// ========================================================================
// UiCrt.cpp compile-time options
// ========================================================================

// the bitmap that holds the screen image is refreshed at 30 Hz.
// there are two different approaches to drawing it:
//     0=blit each character from the fontmap to the bitmap
//     1=construct the m_scrbits image via the rawbmp.h interface
// !DRAW_WITH_BITMAP performs horribly on OSX.  Historically the WXWIN
// situation was the opposite, but on my win7 machine, both approaches
// are about the same performance.
#ifdef __WXMAC__
    // construct the m_scrbits image via the rawbmp.h interface.
    // the performance is glacial on OSX platform if this is not set.
    // (eg, running KALEIDOS, lots of non-blank characters), emulated
    // speed is 0.8 realtime, but with it on it is 12x realtime.
    #define DRAW_WITH_RAWBMP 1
    // this really isn't a choice. wxSound on OSX doesn't support an
    // internally generated wav file.
    #define USE_FILE_BEEPS 1
#else
    #define DRAW_WITH_RAWBMP 0
    #define USE_FILE_BEEPS 0
#endif

// ========================================================================
// UiDiskCtrlCfgDlg.cpp compile-time options
// ========================================================================

// define to true to expose the hacky "auto" disk controller intelligence mode.
// the support is always there, this just exposes it or hides it from the UI.
#define SUPPORT_AUTO_INTELLIGENCE false

// ========================================================================
// miscellaneous
// ========================================================================

// this is the number of IO slots in the backplane
// it is used all over the place
#define NUM_IOSLOTS 6

#endif // _INCLUDE_COMPILE_OPTIONS_H_

// vim: ts=8:et:sw=4:smarttab
