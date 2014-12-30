// Wang BASIC produces cryptic error messages.  The user can double click
// on one of these messages and this table is used to supply the full error
// description, taken from the BASIC User's Manual.

#ifndef _INCLUDE_ERRTABLE_H_
#define _INCLUDE_ERRTABLE_H_

// error description table.
// invalid entries are NULL.

typedef struct {
    char *errcode;      // eg "01", "02", .. "98", "=1", "=2", "=3"
    char *error;        // eg "DIVISION BY 0"
    char *cause;        // eg "The denominator ..."
    char *action;       // eg "Test for zero before dividing"
    char *example;      // eg, "10 D=0\n20 PRINT 5/D"
    char *fix;          // eg, "10 D=0\n20 IF D=0 THEN 30:PRINT 5/D"
} errtable_t;

// for vp errors, the meaning is different
//typedef struct {
//    char *errcode;    // eg "A01", "S20"
//    char *error;      // eg "DIVISION BY 0"
//    char *cause;      // eg "The denominator ..."
//    char *recovery;   // eg "Test for zero before dividing"
//    char *dummy;      // unused
//    char *dummy;      // unused
//} errtable_t;

extern errtable_t errtable[];
extern errtable_t errtable_vp[];

#endif _INCLUDE_ERRTABLE_H_
