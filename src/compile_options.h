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
// there are two different approaches to drawing it, as each performs
// better on one one platform and poorly on the other.
#ifdef __WXMAC__
    // construct the m_scrbits image via the rawbmp.h interface.
    #define DRAW_WITH_RAWBMP 1
#else
    // blit each character from the fontmap to the bitmap.
    // rawbmp is very slow on win32 because rawbmp scribbles on DIBs, but the
    // conversion of DIB to DDB under win32 is really slow when it comes time
    // to blast it to the screen.
    #define DRAW_WITH_RAWBMP 0
#endif

// FORCE_FONTMAP == 0
//    the display is drawn a string at a time, if possible.
// FORCE_FONTMAP == 1
//    the font is pre-rendered in to a bitmap, and a characters are
//    copied over a character at a time.  This may be faster or slower.
#define FORCE_FONTMAP 1

// ========================================================================
// UiCrt_Keyboard.cpp compile-time options
// ========================================================================

// define to 1 to make these keyboard aliases that are useful for
// edit mode.  it is especially useful on the mac, where the function
// keys aren't supported well.
//      CTRL-E --> EDIT
//      CTRL-F --> SF-15 (recall)
//      CTRL-K --> SF-8  (erase)
//      CTRL-D --> SF-9  (delete)
//      CTRL-I --> SF-10 (insert)
#ifdef __WXMAC__
    #define EXTRA_EDIT_KEYS 1
#else
    #define EXTRA_EDIT_KEYS 1
#endif

// ========================================================================
// UiDiskCtrlCfgDlg.cpp compile-time options
// ========================================================================

// define to 1 to expose the hacky "auto" disk controller intelligence mode.
// the support is always there, this just exposes it or hides it from the UI.
#define SUPPORT_AUTO_INTELLIGENCE 0

// ========================================================================
// miscellaneous
// ========================================================================

// this is the number of IO slots in the backplane
// it is used all over the place
#define NUM_IOSLOTS 6

// ========================================================================
// heap leak checker control
// ========================================================================

// this enables/disables leaked heap objects globally in the project.
// actually, it is all always checked, this just enables better diagnostics
// when leaks do happen.  if it is set to 0, only the Ui* code has good
// diagnostics.  The downside is that it exposes all of the wx include
// files to the non-GUI code too, making compiles twice as long.
#define GLOBAL_NEW_DEBUGGING 0

#endif _INCLUDE_COMPILE_OPTIONS_H_

// vim: ts=8:et:sw=4:smarttab
