// Wang BASIC produces cryptic error messages.  The user can double click
// on one of these messages and this table is used to supply the full error
// description, taken from the BASIC User's Manual.

#ifndef _INCLUDE_ERROR_TABLE_H_
#define _INCLUDE_ERROR_TABLE_H_

#include "w2200.h"

// error description table.
// invalid entries are nullptr.

struct error_table_t {
    const char *errcode;    // eg "01", "02", .. "98", "=1", "=2", "=3"
    const char *error;      // eg "DIVISION BY 0"
    const char *cause;      // eg "The denominator ..."
    const char *action;     // eg "Test for zero before dividing"
    const char *example;    // eg, "10 D=0\n20 PRINT 5/D"
    const char *fix;        // eg, "10 D=0\n20 IF D=0 THEN 30:PRINT 5/D"
};

// for vp errors, the meaning is different
//struct error_table_t {
//    const char *errcode;    // eg "A01", "S20"
//    const char *error;      // eg "DIVISION BY 0"
//    const char *cause;      // eg "The denominator ..."
//    const char *recovery;   // eg "Test for zero before dividing"
//    const char *dummy;      // unused
//    const char *dummy;      // unused
//};

extern const std::vector<error_table_t> error_table;
extern const std::vector<error_table_t> error_table_vp;

#endif // _INCLUDE_ERROR_TABLE_H_

// vim: ts=8:et:sw=4:smarttab
