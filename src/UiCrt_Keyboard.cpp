// This is not its own class -- it just implements the keyboard part of
// the Crt class.  It catches keyboard events and then maps that into the
// emulated function.

// ----------------------------------------------------------------------------
// headers
// ----------------------------------------------------------------------------

#include "Ui.h"                 // emulator interface
#include "IoCardKeyboard.h"     // to pick up core_* keyboard functions
#include "System2200.h"
#include "UiSystem.h"           // sharing info between UI_wxgui modules
#include "UiCrtFrame.h"         // emulated terminal
#include "UiCrt.h"              // just the display part
#include "tokens.h"             // keymap tokens

// ----------------------------------------------------------------------------
// code
// ----------------------------------------------------------------------------

// PC_keycode (shift/non-shift flag) -->  Wang_keycode Keyword/A, Wang_keycode A/a
#define KC_ANY      0x0000      // this mapping applies any time
#define KC_NOSHIFT  0x0001      // this mapping applies only if SHIFT isn't present
#define KC_SHIFT    0x0002      // this mapping applies only if SHIFT is present
#define KC_NOCTRL   0x0004      // this mapping applies only if CONTROL isn't present
#define KC_CTRL     0x0008      // this mapping applies only if CONTROL is present

typedef struct {
    int wxKey;
    int wxKeyFlags;
    int wangKey;
} kd_keymap_t;

kd_keymap_t keydown_keymap_table[] = {

    // --------- various control keys -------------
    // key              modifier                        mapping

    { WXK_BACK,         KC_ANY,                         0x08 },

    { WXK_RETURN,       KC_ANY,                         0x0D },
    { WXK_NUMPAD_ENTER, KC_ANY,                         0x0D },

    // clear line
    { WXK_HOME,         KC_ANY,                         0xE5 },

    // next highest line #
    { WXK_TAB,          KC_ANY,                         0xE6 },

    // halt/step
#ifndef __WXMAC__
    { WXK_PAUSE,        KC_ANY,                         IoCardKeyboard::KEYCODE_HALT },
#endif
    { 'C',              KC_CTRL,                        IoCardKeyboard::KEYCODE_HALT },

    // --------- special function keys -------------
    // key              modifier                        mapping

    { WXK_ESCAPE,       KC_NOSHIFT,                     IoCardKeyboard::KEYCODE_SF | 0x00 },
    { WXK_ESCAPE,       KC_SHIFT,                       IoCardKeyboard::KEYCODE_SF | 0x10 },

    { WXK_F1,           KC_NOSHIFT,                     IoCardKeyboard::KEYCODE_SF | 0x01 },
    { WXK_F1,           KC_SHIFT,                       IoCardKeyboard::KEYCODE_SF | 0x11 },

    { WXK_F2,           KC_NOSHIFT,                     IoCardKeyboard::KEYCODE_SF | 0x02 },
    { WXK_F2,           KC_SHIFT,                       IoCardKeyboard::KEYCODE_SF | 0x12 },

    { WXK_F3,           KC_NOSHIFT,                     IoCardKeyboard::KEYCODE_SF | 0x03 },
    { WXK_F3,           KC_SHIFT,                       IoCardKeyboard::KEYCODE_SF | 0x13 },

    { WXK_F4,           KC_NOSHIFT,                     IoCardKeyboard::KEYCODE_SF | 0x04 },
    { WXK_F4,           KC_SHIFT,                       IoCardKeyboard::KEYCODE_SF | 0x14 },

    { WXK_F5,           KC_NOSHIFT,                     IoCardKeyboard::KEYCODE_SF | 0x05 },
    { WXK_F5,           KC_SHIFT,                       IoCardKeyboard::KEYCODE_SF | 0x15 },

    { WXK_F6,           KC_NOSHIFT,                     IoCardKeyboard::KEYCODE_SF | 0x06 },
    { WXK_F6,           KC_SHIFT,                       IoCardKeyboard::KEYCODE_SF | 0x16 },

    { WXK_F7,           KC_NOSHIFT,                     IoCardKeyboard::KEYCODE_SF | 0x07 },
    { WXK_F7,           KC_SHIFT,                       IoCardKeyboard::KEYCODE_SF | 0x17 },

    { WXK_F8,           KC_NOSHIFT,                     IoCardKeyboard::KEYCODE_SF | 0x08 },
    { WXK_F8,           KC_SHIFT,                       IoCardKeyboard::KEYCODE_SF | 0x18 },

#if 1 // !defined(__WXMAC__)  // OS X hijacks these keys, but leaving them defined doesn't hurt
    { WXK_F9,           KC_NOSHIFT | KC_NOCTRL,         IoCardKeyboard::KEYCODE_SF | 0x09 },
    { WXK_F9,           KC_SHIFT   | KC_NOCTRL,         IoCardKeyboard::KEYCODE_SF | 0x19 },
    { WXK_F9,           KC_NOSHIFT | KC_CTRL,           IoCardKeyboard::KEYCODE_SF | 0x0D },
    { WXK_F9,           KC_SHIFT   | KC_CTRL,           IoCardKeyboard::KEYCODE_SF | 0x1D },

    { WXK_F10,          KC_NOSHIFT | KC_NOCTRL,         IoCardKeyboard::KEYCODE_SF | 0x0A },
    { WXK_F10,          KC_SHIFT   | KC_NOCTRL,         IoCardKeyboard::KEYCODE_SF | 0x1A },
    { WXK_F10,          KC_NOSHIFT | KC_CTRL,           IoCardKeyboard::KEYCODE_SF | 0x0E },
    { WXK_F10,          KC_SHIFT   | KC_CTRL,           IoCardKeyboard::KEYCODE_SF | 0x1E },

    { WXK_F11,          KC_NOSHIFT | KC_NOCTRL,         IoCardKeyboard::KEYCODE_SF | 0x0B },
    { WXK_F11,          KC_SHIFT   | KC_NOCTRL,         IoCardKeyboard::KEYCODE_SF | 0x1B },
    { WXK_F11,          KC_NOSHIFT | KC_CTRL,           IoCardKeyboard::KEYCODE_SF | 0x0F },
    { WXK_F11,          KC_SHIFT   | KC_CTRL,           IoCardKeyboard::KEYCODE_SF | 0x1F },
#endif

    { WXK_F12,          KC_NOSHIFT | KC_NOCTRL,         IoCardKeyboard::KEYCODE_SF | 0x0C },
    { WXK_F12,          KC_SHIFT   | KC_NOCTRL,         IoCardKeyboard::KEYCODE_SF | 0x1C },
    { WXK_F12,          KC_CTRL,                        IoCardKeyboard::KEYCODE_SF | IoCardKeyboard::KEYCODE_EDIT },

    // the next three are just in case somebody has a keyboard with 16 Fn keys
    { WXK_F13,          KC_NOSHIFT,                     IoCardKeyboard::KEYCODE_SF | 0x0D },
    { WXK_F13,          KC_SHIFT,                       IoCardKeyboard::KEYCODE_SF | 0x1D },
    { WXK_F14,          KC_NOSHIFT,                     IoCardKeyboard::KEYCODE_SF | 0x0E },
    { WXK_F14,          KC_SHIFT,                       IoCardKeyboard::KEYCODE_SF | 0x1E },
    { WXK_F15,          KC_NOSHIFT,                     IoCardKeyboard::KEYCODE_SF | 0x0F },
    { WXK_F15,          KC_SHIFT,                       IoCardKeyboard::KEYCODE_SF | 0x1F },

#ifdef __WXMAC__
    { WXK_F16,          KC_NOSHIFT | KC_CTRL,           IoCardKeyboard::KEYCODE_SF | IoCardKeyboard::KEYCODE_EDIT },
    { WXK_F16,          KC_SHIFT   | KC_CTRL,           IoCardKeyboard::KEYCODE_SF | IoCardKeyboard::KEYCODE_EDIT },
#endif

    // --------- alias for special function keys -------------
    // these are useful in EDIT mode, as they are much easier to remember
    // key              modifier                        mapping

    // skip one or five spaces left
    { WXK_LEFT,         KC_NOSHIFT,                     IoCardKeyboard::KEYCODE_SF   | 0x0D },
    { WXK_LEFT,         KC_SHIFT,                       IoCardKeyboard::KEYCODE_SF   | 0x0E },

    // skip one or five spaces right
    { WXK_RIGHT,        KC_NOSHIFT,                     IoCardKeyboard::KEYCODE_SF   | 0x0C },
    { WXK_RIGHT,        KC_SHIFT,                       IoCardKeyboard::KEYCODE_SF   | 0x0B },

    // insert a character
    { WXK_INSERT,       KC_ANY,                         IoCardKeyboard::KEYCODE_SF   | 0x0A },
    // delete a character
    { WXK_DELETE,       KC_ANY,                         IoCardKeyboard::KEYCODE_SF   | 0x09 },
    // erase to end of line
    { WXK_END,          KC_ANY,                         IoCardKeyboard::KEYCODE_SF   | 0x08 },

#if EXTRA_EDIT_KEYS
    { 'E',              KC_CTRL,                        IoCardKeyboard::KEYCODE_SF | IoCardKeyboard::KEYCODE_EDIT },      // edit
    { 'F',              KC_CTRL,                        IoCardKeyboard::KEYCODE_SF | 0x0F },              // recall
    { 'D',              KC_CTRL,                        IoCardKeyboard::KEYCODE_SF | 0x09 },              // delete
    { 'I',              KC_CTRL,                        IoCardKeyboard::KEYCODE_SF | 0x0A },              // insert
    { 'K',              KC_CTRL,                        IoCardKeyboard::KEYCODE_SF | 0x08 },              // erase (kill)
#endif

    // --------- misc -------------
    // these don't have any natural place to put them, but they are useful,
    // so just make them mnemonic

    { 'P',              KC_CTRL,                        TOKEN_PRINT },
    { 'L',              KC_CTRL,                        TOKEN_LIST },
    { 'R',              KC_CTRL,                        TOKEN_RUN },
    { 'Z',              KC_CTRL,                        TOKEN_CONTINUE },
/*
 -- numeric keypad:
    NUM_LOCK,           TOKEN_PRINT,
    NUM_LOCK+SHIFT,     TOKEN_ARC,
    '(',                TOKEN_SIN,
    ')',                TOKEN_COS,
    '^',                TOKEN_TAN,
    '*',                TOKEN_SQR,
    '-',                TOKEN_EXP,
    '+',                TOKEN_LOG,
    '/',                TOKEN_PI,

 -- figure out how to map these in:
    ',',                TOKEN_RENUMBER,
    '.',                TOKEN_LIST,

 -- not mapped:
    CLEAR, LOAD
*/
};


typedef struct {
    int wxKey;
    int wangKey_KW_mode;
    int wangKey_Aa_mode;
} oc_keymap_t;

oc_keymap_t onchar_keymap_table[] = {

    // key      Keyword/A mapping       A/a mapping

    { 'a',      'A',                    'a' },
    { 'A',      TOKEN_HEX,              'A' },

    { 'b',      'B',                    'b' },
    { 'B',      TOKEN_SKIP,             'B' },

    { 'c',      'C',                    'c' },
    { 'C',      TOKEN_REWIND,           'C' },

    { 'd',      'D',                    'd' },
    { 'D',      TOKEN_DATA,             'D' },

    { 'e',      'E',                    'e' },
    { 'E',      TOKEN_DEFFN,            'E' },

    { 'f',      'F',                    'f' },
    { 'F',      TOKEN_RESTORE,          'F' },

    { 'g',      'G',                    'g' },
    { 'G',      TTOKEN_READ,            'G' },

    { 'h',      'H',                    'h' },
    { 'H',      TOKEN_IF,               'H' },

    { 'i',      'I',                    'i' },
    { 'I',      TOKEN_FOR,              'I' },

    { 'j',      'J',                    'j' },
    { 'J',      TOKEN_THEN,             'J' },

    { 'k',      'K',                    'k' },
    { 'K',      TOKEN_STOP,             'K' },

    { 'l',      'L',                    'l' },
    { 'L',      TOKEN_END,              'L' },

    { 'm',      'M',                    'm' },
    { 'M',      TOKEN_GOTO,             'M' },

    { 'n',      'N',                    'n' },
    { 'N',      TOKEN_TRACE,            'N' },

    { 'o',      'O',                    'o' },
    { 'O',      TOKEN_STEP,             'O' },

    { 'p',      'P',                    'p' },
    { 'P',      TOKEN_NEXT,             'P' },

    { 'q',      'Q',                    'q' },
    { 'Q',      TOKEN_COM,              'Q' },

    { 'r',      'R',                    'r' },
    { 'R',      TOKEN_GOSUB,            'R' },

    { 's',      'S',                    's' },
    { 'S',      TOKEN_STR,              'S' },

    { 't',      'T',                    't' },
    { 'T',      TOKEN_RETURN,           'T' },

    { 'u',      'U',                    'u' },
    { 'U',      TOKEN_INPUT,            'U' },

    { 'v',      'V',                    'v' },
    { 'V',      TOKEN_SAVE,             'V' },

    { 'w',      'W',                    'w' },
    { 'W',      TOKEN_DIM,              'W' },

    { 'x',      'X',                    'x' },
    { 'X',      TOKEN_BACKSPACE,        'X' },

    { 'y',      'Y',                    'y' },
    { 'Y',      TOKEN_REM,              'Y' },

    { 'z',      'Z',                    'z' },
    { 'Z',      TOKEN_SELECT,           'Z' },
};


void
Crt::OnKeyDown(wxKeyEvent &event)
{
    // don't swallow keystrokes that we can't handle
    if (event.AltDown()) {
        event.Skip();
        return;
    }

    int wxKey = event.GetKeyCode();
    int shift = event.ShiftDown();
    int ctrl  = event.ControlDown();
    int key = 0x00;    // key value we stuff into emulator

    bool foundmap = false;
    for(auto const &kkey : keydown_keymap_table) {
        if (kkey.wxKey != wxKey) {
            continue;
        }
        if ( shift && ((kkey.wxKeyFlags & KC_NOSHIFT) != 0)) {
            continue;
        }
        if (!shift && ((kkey.wxKeyFlags & KC_SHIFT) != 0)) {
            continue;
        }
        if ( ctrl  && ((kkey.wxKeyFlags & KC_NOCTRL) != 0)) {
            continue;
        }
        if (!ctrl  && ((kkey.wxKeyFlags & KC_CTRL) != 0)) {
            continue;
        }
        key = kkey.wangKey;
        foundmap = true;
    }

#if 0
    extern int g_dbg;
    if (key == (0x01 | IoCardKeyboard::KEYCODE_SF)) {
        // key = 'A';
        if (!g_dbg) { UI_Info("Turning on logging to w2200dbg.log"); }
        g_dbg = 1;
        return;  // swallow it
    }
    if (key == (0x02 | IoCardKeyboard::KEYCODE_SF)) {
        // key = 'B';
        if (g_dbg) { UI_Info("Turning off logging to w2200dbg.log"); }
        g_dbg = 0;
        return;  // swallow it
    }
#endif

    if (foundmap) {
        System2200().kb_keystroke(m_parent->getTiedAddr(),
                                  m_parent->getTermNum(),
                                  key);
    } else {
        // let the OnChar routine handle it
        event.Skip();
    }
}


void
Crt::OnChar(wxKeyEvent &event)
{
    bool smart_term = (m_screen_type == UI_SCREEN_2236DE);

    // don't swallow keystrokes that we can't handle
    if (event.AltDown() || event.ControlDown()) {
        event.Skip();
        return;
    }

    int wxKey = event.GetKeyCode();
    int key = 0x00;    // keep lint happy

    bool foundmap = false;
    bool keyword_mode = m_parent->getKeywordMode();
    if (smart_term) {
        // the 2236 doesn't support keyword mode, just caps lock
        if (keyword_mode && ('a' <= wxKey && wxKey <= 'z')) {
            key = wxKey - 'a' + 'A';  // force to uppercase
            foundmap = true;
        }
    } else {
        // the first generation keyboards had a keyword associated with
        // each letter A-Z
        for(auto const &kkey : onchar_keymap_table) {
            if (kkey.wxKey == wxKey) {
                key = (keyword_mode) ? kkey.wangKey_KW_mode
                                     : kkey.wangKey_Aa_mode;
                foundmap = true;
                break;
            }
        }
    }

    if (!foundmap && (wxKey >= 32) && (wxKey < 128)) {
        // non-mapped simple ASCII key
        key = wxKey;
        foundmap = true;
    }

    if (foundmap) {
        System2200().kb_keystroke(m_parent->getTiedAddr(),
                                  m_parent->getTermNum(),
                                  key);
    } else {
        // calling skip causes the menubar & etc logic to process it
        event.Skip();
    }
}

// vim: ts=8:et:sw=4:smarttab
