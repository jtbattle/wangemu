// This is not its own class -- it just implements the keyboard part of
// the Crt class.  It catches keyboard events and then maps that into the
// emulated function.

// ----------------------------------------------------------------------------
// headers
// ----------------------------------------------------------------------------

#include "IoCardKeyboard.h"     // to pick up core_* keyboard functions
#include "TerminalState.h"      // m_crt_state definition
#include "Ui.h"                 // emulator interface
#include "UiCrt.h"              // just the display part
#include "UiCrtFrame.h"         // emulated terminal
#include "UiSystem.h"           // sharing info between UI_wxgui modules
#include "system2200.h"
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

struct kd_keymap_t {
    int wxKey;
    int wxKeyFlags;
    int wangKey;
};

static constexpr kd_keymap_t keydown_keymap_table[] = {

    // --------------------------- keyword keys -------------------------------
    // most of these don't have a natural mapping, so just use Ctrl-<letter>
    // where the letter is mnemonic and doesn't conflict with other Ctrl keys.
    //
    // key              modifier                        mapping

#ifdef __WXMAC__
    { WXK_CLEAR,        KC_ANY,                         TOKEN_CLEAR },
#endif
    { 'C',              KC_CTRL | KC_NOSHIFT,           TOKEN_CLEAR },
    { 'L',              KC_CTRL | KC_NOSHIFT,           TOKEN_LOAD },
    { 'P',              KC_CTRL | KC_NOSHIFT,           TOKEN_PRINT },
    { 'R',              KC_CTRL | KC_NOSHIFT,           TOKEN_RUN },
    { 'Z',              KC_CTRL | KC_NOSHIFT,           TOKEN_CONTINUE },

    // ----------------------- various control keys ---------------------------
    // key              modifier                        mapping

    { WXK_BACK,         KC_ANY,                         0x08 },

    { WXK_RETURN,       KC_ANY,                         0x0D },
    { WXK_NUMPAD_ENTER, KC_ANY,                         0x0D },

    // clear line
    { WXK_HOME,         KC_ANY,                         0xE5 },

    // next highest line #  (in 6367 keyboard controller mode)
    // FN (in MXD mode; Terminal.cpp takes care of remapping it)
    { WXK_TAB,          KC_ANY,                         0xE6 },

    // halt/step
    { 'S', /*step*/     KC_CTRL | KC_NOSHIFT,           IoCardKeyboard::KEYCODE_HALT },
#ifdef __WXMSW__
    { WXK_PAUSE,        KC_ANY,                         IoCardKeyboard::KEYCODE_HALT },
#endif

    // ----------------------- special function keys ---------------------------
    // key              modifier                        mapping

    { WXK_ESCAPE,       KC_NOSHIFT,                     IoCardKeyboard::KEYCODE_SF | 0x00 },
    { WXK_ESCAPE,       KC_SHIFT,                       IoCardKeyboard::KEYCODE_SF | 0x10 },

    { WXK_F1,           KC_NOSHIFT,                     IoCardKeyboard::KEYCODE_SF | 0x01 },
    { WXK_F1,           KC_SHIFT,                       IoCardKeyboard::KEYCODE_SF | 0x11 },

    { WXK_F2,           KC_NOSHIFT,                     IoCardKeyboard::KEYCODE_SF | 0x02 },
    { WXK_F2,           KC_SHIFT,                       IoCardKeyboard::KEYCODE_SF | 0x12 },

    { WXK_F3,           KC_NOSHIFT,                     IoCardKeyboard::KEYCODE_SF | 0x03 },
    { WXK_F3,           KC_SHIFT,                       IoCardKeyboard::KEYCODE_SF | 0x13 },

    // edit mode: end of line
    { WXK_F4,           KC_NOSHIFT,                     IoCardKeyboard::KEYCODE_SF | 0x04 },
    { WXK_F4,           KC_SHIFT,                       IoCardKeyboard::KEYCODE_SF | 0x14 },
#ifdef __WXMAC__
    { WXK_DOWN,         KC_SHIFT,                       IoCardKeyboard::KEYCODE_SF | 0x04 },
#else
    { WXK_RIGHT,        KC_CTRL,                        IoCardKeyboard::KEYCODE_SF | 0x04 },
#endif

    // edit mode: down a line
    { WXK_DOWN,         KC_NOSHIFT,                     IoCardKeyboard::KEYCODE_SF | 0x45 },
    { WXK_F5,           KC_NOSHIFT,                     IoCardKeyboard::KEYCODE_SF | 0x05 },
    { WXK_F5,           KC_SHIFT,                       IoCardKeyboard::KEYCODE_SF | 0x15 },

    // edit mode: up a line
    { WXK_UP,           KC_NOSHIFT,                     IoCardKeyboard::KEYCODE_SF | 0x46 },
    { WXK_F6,           KC_NOSHIFT,                     IoCardKeyboard::KEYCODE_SF | 0x06 },
    { WXK_F6,           KC_SHIFT,                       IoCardKeyboard::KEYCODE_SF | 0x16 },

    // edit mode: beginning of line
    { WXK_F7,           KC_NOSHIFT,                     IoCardKeyboard::KEYCODE_SF | 0x07 },
    { WXK_F7,           KC_SHIFT,                       IoCardKeyboard::KEYCODE_SF | 0x17 },
#ifdef __WXMAC__
    { WXK_UP,           KC_SHIFT,                       IoCardKeyboard::KEYCODE_SF | 0x07 },
#else
    { WXK_LEFT,         KC_CTRL,                        IoCardKeyboard::KEYCODE_SF | 0x07 },
#endif

    // edit mode: erase to end of line
    { 'K',              KC_CTRL,                        IoCardKeyboard::KEYCODE_SF | 0x08 },
    { WXK_END,          KC_ANY,                         IoCardKeyboard::KEYCODE_SF | 0x08 },
    { WXK_F8,           KC_NOSHIFT,                     IoCardKeyboard::KEYCODE_SF | 0x08 },
    { WXK_F8,           KC_SHIFT,                       IoCardKeyboard::KEYCODE_SF | 0x18 },

    // edit mode: delete a character
    { 'D',              KC_CTRL,                        IoCardKeyboard::KEYCODE_SF | 0x09 },
    { WXK_DELETE,       KC_NOSHIFT,                     IoCardKeyboard::KEYCODE_SF | 0x49 },
	{ WXK_DELETE,       KC_SHIFT,                       IoCardKeyboard::KEYCODE_SF | 0x59 },
	{ WXK_F9,           KC_NOSHIFT | KC_NOCTRL,         IoCardKeyboard::KEYCODE_SF | 0x09 },
    { WXK_F9,           KC_SHIFT   | KC_NOCTRL,         IoCardKeyboard::KEYCODE_SF | 0x19 },

    // edit mode: insert a character
    { 'I',              KC_CTRL,                        IoCardKeyboard::KEYCODE_SF | 0x0A },
	{ WXK_INSERT,       KC_NOSHIFT,                     IoCardKeyboard::KEYCODE_SF | 0x4A },
	{ WXK_INSERT,       KC_SHIFT,                       IoCardKeyboard::KEYCODE_SF | 0x5A },
    { WXK_F10,          KC_NOSHIFT | KC_NOCTRL,         IoCardKeyboard::KEYCODE_SF | 0x0A },
    { WXK_F10,          KC_SHIFT   | KC_NOCTRL,         IoCardKeyboard::KEYCODE_SF | 0x1A },

    // edit mode: skip five spaces right
    { WXK_RIGHT,        KC_SHIFT,                       IoCardKeyboard::KEYCODE_SF | 0x4C },
    { WXK_F11,          KC_NOSHIFT | KC_NOCTRL,         IoCardKeyboard::KEYCODE_SF | 0x0B },
    { WXK_F11,          KC_SHIFT   | KC_NOCTRL,         IoCardKeyboard::KEYCODE_SF | 0x1B },

    // edit mode: skip one space right
    { WXK_RIGHT,        KC_NOSHIFT,                     IoCardKeyboard::KEYCODE_SF | 0x4C },
    { WXK_F12,          KC_NOSHIFT | KC_NOCTRL,         IoCardKeyboard::KEYCODE_SF | 0x0C },
    { WXK_F12,          KC_SHIFT   | KC_NOCTRL,         IoCardKeyboard::KEYCODE_SF | 0x1C },

    // edit mode: skip one space left
    { WXK_LEFT,         KC_NOSHIFT,                     IoCardKeyboard::KEYCODE_SF | 0x4D },
    { WXK_F13,          KC_NOSHIFT,                     IoCardKeyboard::KEYCODE_SF | 0x0D },
    { WXK_F13,          KC_SHIFT,                       IoCardKeyboard::KEYCODE_SF | 0x1D },
#ifdef __WXMSW__
    { WXK_F9,           KC_NOSHIFT | KC_CTRL,           IoCardKeyboard::KEYCODE_SF | 0x0D },
    { WXK_F9,           KC_SHIFT   | KC_CTRL,           IoCardKeyboard::KEYCODE_SF | 0x1D },
#endif

    // edit mode: skip five spaces left
    { WXK_LEFT,         KC_SHIFT,                       IoCardKeyboard::KEYCODE_SF | 0x4D },
    { WXK_F14,          KC_NOSHIFT,                     IoCardKeyboard::KEYCODE_SF | 0x0E },
    { WXK_F14,          KC_SHIFT,                       IoCardKeyboard::KEYCODE_SF | 0x1E },
#ifdef __WXMSW__
    { WXK_F10,          KC_NOSHIFT | KC_CTRL,           IoCardKeyboard::KEYCODE_SF | 0x0E },
    { WXK_F10,          KC_SHIFT   | KC_CTRL,           IoCardKeyboard::KEYCODE_SF | 0x1E },
#endif

		// Page UP / Down
	{ WXK_PAGEUP,       KC_NOSHIFT,			            IoCardKeyboard::KEYCODE_SF | 0x42 },
	{ WXK_PAGEDOWN,     KC_NOSHIFT,			            IoCardKeyboard::KEYCODE_SF | 0x43 },
	{ WXK_PAGEUP,       KC_SHIFT,			            IoCardKeyboard::KEYCODE_SF | 0x52 },
	{ WXK_PAGEDOWN,     KC_SHIFT,			            IoCardKeyboard::KEYCODE_SF | 0x53 },
		
		// edit mode: recall
    { 'F',              KC_CTRL,                        IoCardKeyboard::KEYCODE_SF | 0x0F },
    { WXK_F15,          KC_NOSHIFT,                     IoCardKeyboard::KEYCODE_SF | 0x0F },
    { WXK_F15,          KC_SHIFT,                       IoCardKeyboard::KEYCODE_SF | 0x1F },
#ifdef __WXMSW__
    { WXK_F11,          KC_NOSHIFT | KC_CTRL,           IoCardKeyboard::KEYCODE_SF | 0x0F },
    { WXK_F11,          KC_SHIFT   | KC_CTRL,           IoCardKeyboard::KEYCODE_SF | 0x1F },
#endif

    // edit mode toggle
    { 'E',              KC_CTRL,                        IoCardKeyboard::KEYCODE_SF | IoCardKeyboard::KEYCODE_EDIT },
#ifdef __WXMSW__
    { WXK_F12,          KC_CTRL,                        IoCardKeyboard::KEYCODE_SF | IoCardKeyboard::KEYCODE_EDIT },
#endif
#ifdef __WXMAC__
    { WXK_F16,          KC_NOSHIFT,                     IoCardKeyboard::KEYCODE_SF | IoCardKeyboard::KEYCODE_EDIT },
#endif

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
*/
};


struct oc_keymap_t {
    int wxKey;
    int wangKey_KW_mode;
    int wangKey_Aa_mode;
};

static constexpr oc_keymap_t onchar_keymap_table[] = {

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
Crt::OnChar(wxKeyEvent &event)
{
    // don't swallow keystrokes that we can't handle
    if (event.AltDown()) {
        event.Skip();
        return;
    }

    const int  wxKey = event.GetKeyCode();
    const bool shift = event.ShiftDown();
    const bool ctrl  = event.RawControlDown();
    // map ctrl-A through ctrl-Z to 'A' to 'Z'
    const int baseKey = (ctrl && (1 <= wxKey) && (wxKey <= 26)) ? (wxKey | 64) : wxKey;
    int key = 0x00;    // key value we stuff into emulator

    bool found_map = false;
    for (auto const &kkey : keydown_keymap_table) {
        if (kkey.wxKey != baseKey) {
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
        found_map = true;
        break;
    }

    if (!found_map) {
        const bool keyword_mode = m_parent->getKeywordMode();
        const bool smart_term = (m_crt_state->screen_type == UI_SCREEN_2236DE);
        if (smart_term) {
            // the 2236 doesn't support keyword mode, just caps lock
            if (keyword_mode && ('a' <= baseKey && baseKey <= 'z')) {
                key = baseKey - 'a' + 'A';  // force to uppercase
                found_map = true;
            }
        } else {
            // the first generation keyboards had a keyword associated with
            // each letter A-Z
            for (auto const &kkey : onchar_keymap_table) {
                if (kkey.wxKey == baseKey) {
                    key = (keyword_mode) ? kkey.wangKey_KW_mode
                                         : kkey.wangKey_Aa_mode;
                    found_map = true;
                    break;
                }
            }
        }
		// Additional Keycodes for internatinal Keyboard (german)
		if (smart_term) {
			if (baseKey == 0xFC) { 
					key = 0x19;	
					found_map = true;
			} else if (baseKey == 0xDC) {
					key = 0x1F;
					found_map = true;
			} else if (baseKey == 0xE4) {
						key = 0x15;
						found_map = true;
			}
			else if (baseKey == 0xC4) {
				key = 0x1D;
				found_map = true;
			}
			else if (baseKey == 0xF6) {
				key = 0x18;
				found_map = true;
			}
			else if (baseKey == 0xD6) {
				key = 0x1E;
				found_map = true;
			}
			else if (baseKey == 0xDF) {
				key = 0x8E;
				found_map = true;
			}
		}

        if (!found_map && (wxKey >= 32) && (wxKey < 128)) {
            // non-mapped simple ASCII key
            key = wxKey;
            found_map = true;
        }
    }
	// Keycode for Underlining, non functioning, spells out atom. (PRINT i.e. A0) 
	/*if (wxKey == '_') {
		system2200::dispatchKeystroke(m_parent->getTiedAddr(),
			m_parent->getTermNum(),
			0x01A0);
	}
	else */ 
	if (found_map) {
        system2200::dispatchKeystroke(m_parent->getTiedAddr(),
                                      m_parent->getTermNum(),
                                      key);
    } else {
        // percolate the event up to the parent
        event.Skip();
    }
}

// vim: ts=8:et:sw=4:smarttab
