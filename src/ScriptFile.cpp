// This file provides a script reading facility.
// Scripts can contain literal text, but there is also an escape
// mechanism for specifying an arbitrary byte value.  The same
// escape mechanism can be used to specify BASIC keywords symbolically.
// Scripts can include other script files, up to some nesting limit.
//
// This started life in my "Solace" Sol-20 emulator as script.c,
// but it has been nearly completely rewritten C++ style.

#include "Host.h"               // for Config* functions
#include "IoCardKeyboard.h"
#include "ScriptFile.h"
#include "Ui.h"                 // needed for UI_Alert()
#include "tokens.h"             // predigested keyword tokens

#include <sstream>
using std::ostringstream;

// =========================================================================
// Open a script file.
// "metaflags" indicates what types of escapes to look for.
// "max_nesting_depth" indicates how deep include file nesting is allowed.
// Use OpenedOK() to determine if the file was opened successfully.

ScriptFile::ScriptFile(const string &filename,
                       int metaflags,
                       int max_nesting_depth,
                       int cur_nesting_depth) :
        m_ifs(nullptr),
        m_filename(""),
        m_opened_ok(false),
        m_eof(false),
        m_metaflags(metaflags),
        m_cur_depth(cur_nesting_depth),
        m_max_depth(max_nesting_depth),
        m_cur_line(0),
        m_subscript(nullptr),
        m_cur_char(0)
{
    // put in canonical format
    m_filename = Host::asAbsolutePath(filename);

    // attempt to open the file for reading; set m_opened_ok accordingly
    // if success, set m_curline=1, read first line, set pointers, set EOF state
    m_ifs = new ifstream( m_filename.c_str(), ifstream::in );

    if (m_ifs && m_ifs->fail()) {
        delete m_ifs;
        m_ifs = nullptr;
    }

    if (!m_ifs) {
        m_opened_ok = false;
        return;
    }
        m_opened_ok = true;

    // attempt to read the first line of the file
    m_prepare_next_line();
}

// =========================================================================
// OK to destruct with open files; they will be cleaned up

ScriptFile::~ScriptFile()
{
    if (m_subscript) {
        delete m_subscript;
        m_subscript = nullptr;
    }

    if (m_ifs) {
        delete m_ifs;
        m_ifs = nullptr;
    }
}

// =========================================================================
// after opening the file, this function should be checked to make
// sure the file exists and is readable.  returns false on error.

bool
ScriptFile::openedOk() const
{
    return m_opened_ok;
}

// =========================================================================
// indicate if all the characters int the file have been returned

bool
ScriptFile::isEof() const
{
    return m_eof;
}

// =========================================================================
// return a string containing the file and line we are about to read.
//    "somescript.txt:17"
// or
//    "somescript.txt:17, included from otherscript.txt:36"

string
ScriptFile::getLineDescription() const
{
    string desc("");

    if (m_subscript) {
        string desc2(m_subscript->getLineDescription());
        desc = desc2 + ",\nincluded from ";
    }

    ostringstream ostr;
    ostr << m_filename << ":" << m_cur_line;
    desc += ostr.str();

    return desc;
}

// =========================================================================
// does necessary manipulations to read next line of text and sets flags

void
ScriptFile::m_prepare_next_line()
{
    m_ifs->getline( m_charbuf, MAX_EXPECTED_LINE_LENGTH);
    if (m_ifs->gcount() == 0) {
        m_charbuf[0] = '\0';
        m_eof = true;
        return;
    }

    // we got something, so count it as a line
    m_cur_line++;

    // if the line is longer than the buffer, the fail bit is set.
    // with some heroics, we could silently deal with this, but for now,
    // just complain.
    if (m_ifs->fail()) {
        string location = getLineDescription();
        string msg = "Very long line in script at " + location;
        UI_Warn( "%s", msg.c_str() );
        m_charbuf[MAX_EXPECTED_LINE_LENGTH] = '\0';  // make sure zero terminated
    }

    // remove any combination of trailing CRs and LFs from buffer
    int len = strlen(m_charbuf);
    while ((m_charbuf[len-1] == '\n' || m_charbuf[len-1] == '\r') && len>0) {
        m_charbuf[len-1] = '\0';
        len--;
    }

    m_cur_char = 0; // point to first char of line
}


// =========================================================================
// helper functions

bool
ScriptFile::ishexdigit(char ch) const
{
    return (ch >= '0' && ch <= '9') ||
           (ch >= 'A' && ch <= 'F') ||
           (ch >= 'a' && ch <= 'f');
}


int
ScriptFile::hexval(char ch) const
{
    if (ch >= '0' && ch <= '9')
        return ch - '0';
    if (ch >= 'A' && ch <= 'F')
        return ch - 'A' + 10;
    if (ch >= 'a' && ch <= 'f')
        return ch - 'a' + 10;
    return -1;
}

// =========================================================================
// define the mapping table

typedef struct {
    char *name;
    short val;          // flags | keycode
} metakeytable_t;

static metakeytable_t metakeytable[] = {

    // useful keys
    { "<LF>",           0x0A },
    { "<CR>",           0x0D },
    { "<DEL>",          0x7F },

    // special function keys
    { "<SF0>",          IoCardKeyboard::KEYCODE_SF | 0x00 },
    { "<SF1>",          IoCardKeyboard::KEYCODE_SF | 0x01 },
    { "<SF2>",          IoCardKeyboard::KEYCODE_SF | 0x02 },
    { "<SF3>",          IoCardKeyboard::KEYCODE_SF | 0x03 },
    { "<SF4>",          IoCardKeyboard::KEYCODE_SF | 0x04 },
    { "<SF5>",          IoCardKeyboard::KEYCODE_SF | 0x05 },
    { "<SF6>",          IoCardKeyboard::KEYCODE_SF | 0x06 },
    { "<SF7>",          IoCardKeyboard::KEYCODE_SF | 0x07 },
    { "<SF8>",          IoCardKeyboard::KEYCODE_SF | 0x08 },
    { "<SF9>",          IoCardKeyboard::KEYCODE_SF | 0x09 },
    { "<SF10>",         IoCardKeyboard::KEYCODE_SF | 0x0A },
    { "<SF11>",         IoCardKeyboard::KEYCODE_SF | 0x0B },
    { "<SF12>",         IoCardKeyboard::KEYCODE_SF | 0x0C },
    { "<SF13>",         IoCardKeyboard::KEYCODE_SF | 0x0D },
    { "<SF14>",         IoCardKeyboard::KEYCODE_SF | 0x0E },
    { "<SF15>",         IoCardKeyboard::KEYCODE_SF | 0x0F },
    { "<SF16>",         IoCardKeyboard::KEYCODE_SF | 0x10 },
    { "<SF17>",         IoCardKeyboard::KEYCODE_SF | 0x11 },
    { "<SF18>",         IoCardKeyboard::KEYCODE_SF | 0x12 },
    { "<SF19>",         IoCardKeyboard::KEYCODE_SF | 0x13 },
    { "<SF20>",         IoCardKeyboard::KEYCODE_SF | 0x14 },
    { "<SF21>",         IoCardKeyboard::KEYCODE_SF | 0x15 },
    { "<SF22>",         IoCardKeyboard::KEYCODE_SF | 0x16 },
    { "<SF23>",         IoCardKeyboard::KEYCODE_SF | 0x17 },
    { "<SF24>",         IoCardKeyboard::KEYCODE_SF | 0x18 },
    { "<SF25>",         IoCardKeyboard::KEYCODE_SF | 0x19 },
    { "<SF26>",         IoCardKeyboard::KEYCODE_SF | 0x1A },
    { "<SF27>",         IoCardKeyboard::KEYCODE_SF | 0x1B },
    { "<SF28>",         IoCardKeyboard::KEYCODE_SF | 0x1C },
    { "<SF29>",         IoCardKeyboard::KEYCODE_SF | 0x1D },
    { "<SF30>",         IoCardKeyboard::KEYCODE_SF | 0x1E },
    { "<SF31>",         IoCardKeyboard::KEYCODE_SF | 0x1F },

    // keywords

    // 0x80
    { "<LIST>",         TOKEN_LIST },
    { "<CLEAR>",        TOKEN_CLEAR },
    { "<RUN>",          TOKEN_RUN },
    { "<RENUMBER>",     TOKEN_RENUMBER },
    { "<CONTINUE>",     TOKEN_CONTINUE },
    { "<SAVE>",         TOKEN_SAVE },
    { "<LIMITS>",       TOKEN_LIMITS },
    { "<COPY>",         TOKEN_COPY },
    { "<KEYIN>",        TOKEN_KEYIN },
    { "<DSKIP>",        TOKEN_DSKIP },
    { "<AND>",          TOKEN_AND },
    { "<OR>",           TOKEN_OR },
    { "<XOR>",          TOKEN_XOR },
    { "<TEMP>",         TOKEN_TEMP },
    { "<DISK>",         TOKEN_DISK },
    { "<TAPE>",         TOKEN_TAPE },

    // 0x90
    { "<TRACE>",        TOKEN_TRACE },
    { "<LET>",          TOKEN_LET },
//  { "<DRAM>",         TOKEN_DRAM },     // what is this one?  it is FIX( for BASIC-2
    { "<FIX(>",         TOKEN_FIX },      // BASIC-2 only
    { "<DIM>",          TOKEN_DIM },
    { "<ON>",           TOKEN_ON },
    { "<STOP>",         TOKEN_STOP },
    { "<END>",          TOKEN_END },
    { "<DATA>",         TOKEN_DATA },
    { "<READ>",         TTOKEN_READ },
    { "<INPUT>",        TOKEN_INPUT },
    { "<GOSUB>",        TOKEN_GOSUB },
    { "<RETURN>",       TOKEN_RETURN },
    { "<GOTO>",         TOKEN_GOTO },
    { "<NEXT>",         TOKEN_NEXT },
    { "<FOR>",          TOKEN_FOR },
    { "<IF>",           TOKEN_IF },

    // 0xA0
    { "<PRINT>",        TOKEN_PRINT },
    { "<LOAD>",         TOKEN_LOAD },
    { "<REM>",          TOKEN_REM },
    { "<RESTORE>",      TOKEN_RESTORE },
    { "<PLOT>",         TOKEN_PLOT },      // =0xA4, PLOT <...> command;
    { "<SELECT>",       TOKEN_SELECT },
    { "<COM>",          TOKEN_COM },
    { "<PRINTUSING>",   TOKEN_PRINTUSING },
    { "<MAT>",          TOKEN_MAT },
    { "<REWIND>",       TOKEN_REWIND },
    { "<SKIP>",         TOKEN_SKIP },
    { "<BACKSPACE>",    TOKEN_BACKSPACE },
    { "<SCRATCH>",      TOKEN_SCRATCH },
    { "<MOVE>",         TOKEN_MOVE },
    { "<CONVERT>",      TOKEN_CONVERT },
    { "<SELECTPLOT>",   TOKEN_SELECT_PLOT }, // = 0xAF, [SELECT] PLOT

    // 0xB0
    { "<STEP>",         TOKEN_STEP },
    { "<THEN>",         TOKEN_THEN },
    { "<TO>",           TOKEN_TO },
    { "<BEG>",          TOKEN_BEG },
    { "<OPEN>",         TOKEN_OPEN },
    { "<CI>",           TOKEN_CI },        // [SELECT] CI
    { "<R>",            TOKEN_R },         // [SELECT] R
    { "<D>",            TOKEN_D },         // [SELECT] D
    { "<CO>",           TOKEN_CO },        // [SELECT] CO
    { "<LGT(>",         TOKEN_LGT },
    { "<OFF>",          TOKEN_OFF },
    { "<DBACKSPACE>",   TOKEN_DBACKSPACE },
    { "<VERIFY>",       TOKEN_VERIFY },
    { "<DA>",           TOKEN_DA },
    { "<BA>",           TOKEN_BA },
    { "<DC>",           TOKEN_DC },

    // 0xC0
    { "<FN>",           TOKEN_FN },
    { "<ABS(>",         TOKEN_ABS },
    { "<SQR(>",         TOKEN_SQR },
    { "<COS(>",         TOKEN_COS },
    { "<EXP(>",         TOKEN_EXP },
    { "<INT(>",         TOKEN_INT },
    { "<LOG(>",         TOKEN_LOG },
    { "<SIN(>",         TOKEN_SIN },
    { "<SGN(>",         TOKEN_SGN },
    { "<RND(>",         TOKEN_RND },
    { "<TAN(>",         TOKEN_TAN },
    { "<ARC>",          TOKEN_ARC },
    { "<#PI>",          TOKEN_PI },
    { "<TAB(>",         TOKEN_TAB },
    { "<DEFFN>",        TOKEN_DEFFN },
    { "<ARCTAN(>",      TOKEN_ARCTAN },

    // 0xD0
    { "<ARCSIN(>",      TOKEN_ARCSIN },
    { "<ARCCOS(>",      TOKEN_ARCCOS },
    { "<HEX(>",         TOKEN_HEX },       // =0xD2, note: there are two HEX tokens
    { "<STR(>",         TOKEN_STR },
    { "<ATN(>",         TOKEN_ATN },
    { "<LEN(>",         TOKEN_LEN },
    { "<RE>",           TOKEN_RE },
    { "<#>",            TOKEN_SHARP },     // [SELECT]#
    { "<%>",            TOKEN_PERCENT },   // %[image]
    { "<P>",            TOKEN_P },         // [SELECT] P
    { "<BT>",           TOKEN_BT },
    { "<G>",            TOKEN_G },         // [SELECT] G
    { "<VAL(>",         TOKEN_VAL },
    { "<NUM(>",         TOKEN_NUM },
    { "<BIN(>",         TOKEN_BIN },
    { "<POS(>",         TOKEN_POS },

    // 0xE0
    { "<LS=>",          TOKEN_LSEQ },
    { "<ALL>",          TOKEN_ALL },
    { "<PACK>",         TOKEN_PACK },
    { "<CLOSE>",        TOKEN_CLOSE },
    { "<INIT>",         TOKEN_INIT },
//  { "<HEX>",          TOKEN_HEX_ANOTHER },       = 0xE5,
    { "<UNPACK>",       TOKEN_UNPACK },
    { "<BOOL>",         TOKEN_BOOL },
    { "<ADD>",          TOKEN_ADD },
    { "<ROTATE>",       TOKEN_ROTATE },
    { "<$>",            TOKEN_DOLLAR },    // $[stmt]

// the following are VP-specific (some of the above too)
    { "<ERROR>",        TOKEN_ERROR },
    { "<ERR>",          TOKEN_ERR },
    { "<DAC>",          TOKEN_DAC },
    { "<DSC>",          TOKEN_DSC },
    { "<SUB>",          TOKEN_SUB },

    // 0xF0
    { "<LINPUT>",       TOKEN_LINPUT },
    { "<VER(>",         TOKEN_VER },
    { "<ELSE>",         TOKEN_ELSE },
    { "<SPACE>",        TOKEN_SPACE },
    { "<ROUND>",        TOKEN_ROUND },
    { "<AT(>",          TOKEN_AT },
    { "<HEXOF(>",       TOKEN_HEXOF },
    { "<MAX(>",         TOKEN_MAX },
    { "<MIN(>",         TOKEN_MIN },
    { "<MOD(>",         TOKEN_MOD },
//  { "<xFA>",          TOKEN_FA_RESERVED },
//  { "<xFB>",          TOKEN_FB_RESERVED },
//  { "<xFC>",          TOKEN_FC_RESERVED },
//  { "<xFD>",          TOKEN_FD_RESERVED },
//  { "<xFE>",          TOKEN_FE_RESERVED },
//  { "<xFF>",          TOKEN_FF_PACKED_LINE_NUMBER }
};

// =========================================================================
// fetch the next byte of the stream in "*byte".
// return true if successful, and false if EOF before the read is attempted.

bool
ScriptFile::getNextByte(int *byte)
{
    // we may need to process more than one line of input to produce
    // one byte of output, due to include files
    for (;;) {

        // if we have a subscript running, listen to it
        if (m_subscript) {
            if (m_subscript->isEof()) {
                delete m_subscript;
                m_subscript = nullptr;
                continue;  // retry using current script processing
            }
            return m_subscript->getNextByte(byte);
        }

        if (isEof())
            return false;

        if (m_charbuf[m_cur_char] == 0) {
            // we hit the end of line
            *byte = 0x0D;   // carriage return
            m_prepare_next_line();
            return true;
        }

        char ch = m_charbuf[m_cur_char++];

        // look for literal characters
        if (ch != '\\') {
            *byte = ch;
            return true;
        }

        // Escape case #1: "\\" -> "\"
        if (m_charbuf[m_cur_char] == '\\') {
            *byte = '\\';
            m_cur_char++;
            return true;
        }

        // Escape case #2: "\xx" -> interpret as hex value of char
        if (m_metaflags & SCRIPT_META_HEX) {
            if ( ishexdigit(m_charbuf[m_cur_char+0]) &&
                 ishexdigit(m_charbuf[m_cur_char+1]) ) {
                int val = 16*hexval(m_charbuf[m_cur_char+0]) +
                             hexval(m_charbuf[m_cur_char+1]) ;
                *byte = val;
                m_cur_char += 2;
                return true;
            }
        }

        // Escape case #3: "\<label>" -> map using symbol table
        if (m_metaflags & SCRIPT_META_KEY) {
            for(unsigned int i=0; i<sizeof(metakeytable)/sizeof(metakeytable_t); i++) {
                int len = strlen(metakeytable[i].name);
                if (strncmp( &m_charbuf[m_cur_char], metakeytable[i].name, len) == 0) {
                    // we found a matcher
                    *byte = metakeytable[i].val;
                    m_cur_char += len;
                    return true;
                }
            }
        }

        // check for include file of the form
        // \<include filename.foo>
        // it must start a line in the first column.
        // any chars after the closing ">" are ignored.
        if ( (m_metaflags & SCRIPT_META_INC) &&
             (m_cur_depth < m_max_depth) &&
             (strncmp( &m_charbuf[m_cur_char], "<include ", 9) == 0)) {

            // scan for either end of line or ">"
            m_cur_char += 9;
            int end_char;
            for(end_char = m_cur_char; m_charbuf[end_char] && m_charbuf[end_char]!='>'; end_char++)
                ;
            if (m_charbuf[end_char] == '>') {
                m_charbuf[end_char] = '\0';  // terminate filename
                string inc_fname( &m_charbuf[m_cur_char] );
                m_cur_char = end_char+1;

                // if the include file name isn't absolute, turn it to an
                // absolute path, relative to the location of the current script
                string abs_inc_fname;
                if (Host::isAbsolutePath(inc_fname)) {
                    abs_inc_fname = inc_fname;
                } else {
                    // determine the path to the current script
                    string tmp_fname;
#if __WXMSW__
                    size_t last_sep = m_filename.find_last_of("/\\");
                    tmp_fname = m_filename.substr(0,last_sep+1) + inc_fname;
#else
                    size_t last_sep = m_filename.find_last_of("/");
                    tmp_fname = m_filename.substr(0,last_sep+1) + inc_fname;
#endif
                    abs_inc_fname = Host::asAbsolutePath(tmp_fname);
                }

                // do include processing ...
                m_subscript = new ScriptFile( abs_inc_fname, m_metaflags,
                                              m_max_depth, m_cur_depth+1 );

                if (!m_subscript->openedOk()) {
                    delete m_subscript;
                    m_subscript = nullptr;
                    string location = getLineDescription();
                    string msg("Error opening file '" + abs_inc_fname +
                               "',\nincluded from " + location);
                    UI_Error("%s", msg.c_str());
                    m_prepare_next_line();
                    continue;  // try next line of input
                }

                // it opened OK; start slurping from it
                continue;
            } // if ('>')
        } // if (SCRIPT_META_INC)

        // rather than complaining obtrusively, just echo it literally
        *byte = ch;
        return true;

    } // end while
}

// vim: ts=8:et:sw=4:smarttab
