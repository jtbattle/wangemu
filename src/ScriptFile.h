// Open a text file for later character-at-a-time streaming.
// Optionally look for escaped characters and include'd files.

#ifndef _INCLUDE_SCRIPT_H_
#define _INCLUDE_SCRIPT_H_

#include "w2200.h"
#include <fstream>
#include <memory>

class ScriptFile
{
public:
    CANT_ASSIGN_OR_COPY_CLASS(ScriptFile);

    // options when opening file
    enum { SCRIPT_META_KEY = 0x0001,  // interpret \<LOAD> and the like
           SCRIPT_META_HEX = 0x0002,  // interpret \3F and the like
           SCRIPT_META_INC = 0x0004   // interpret \include filename.foo
         };

    // Open a script file.
    // "metaflags" indicates what types of escapes to look for.
    // "max_nesting_depth" indicates how deep include file nesting is allowed.
    // Use OpenedOK() to determine if the file was opened successfully.
    ScriptFile(const std::string &filename, int metaflags,
               int max_nesting_depth=1, int cur_nesting_depth=1);

    // OK to destruct with open files; they will be cleaned up
    ~ScriptFile();

    // after opening the file, this function should be checked to make
    // sure the file exists and is readable.  returns false on error.
    bool openedOk() const noexcept;

    // indicate if all the characters int the file have been returned
    bool isEof() const noexcept;

    // return a string containing the file and line we are about to read.
    //    "somescript.txt:17"
    // or
    //    "somescript.txt:17, included from otherscript.txt:36"
    std::string getLineDescription() const;

    // fetch the next byte of the stream in "*byte".
    // return true if successful, and false if EOF before the read is attempted.
    bool getNextByte(int *byte);

private:
    static const int MAX_EXPECTED_LINE_LENGTH=1024;  // getline() needs a char* buffer

    std::unique_ptr<std::ifstream> m_ifs;  // input file stream
    std::string     m_filename;     // name of opened script file
    bool            m_opened_ok;    // residual state of attempt to open the script file
    bool            m_eof;          // we've hit the end of file

    int             m_metaflags;    // which escapes to recognize

    int             m_cur_depth;    // now deeply nested we are (starting at 1)
    int             m_max_depth;    // how deeply nesting is allowed

    int             m_cur_line;     // current line of file (starts at 1)

    std::unique_ptr<ScriptFile> m_subscript;    // the file we are pending on (or 0 if none)

    // does necessary manipulations to read next line of text and sets flags
    void            m_prepare_next_line();
    char            m_charbuf[MAX_EXPECTED_LINE_LENGTH+1];  // +1 for termination
    int             m_cur_char;     // which character to return next

    // helpers
    bool ishexdigit(char ch) const noexcept;
    int  hexval(char ch) const noexcept;
};

#endif // ifdef _INCLUDE_SCRIPT_H_

// vim: ts=8:et:sw=4:smarttab
