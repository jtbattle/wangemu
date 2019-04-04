
#include "error_table.h"   // pick up error_table_t definition

// #########################################################################
// ##                          Wang BASIC Errors                          ##
// #########################################################################

const std::vector<error_table_t> error_table = {

// -------------------------------------------------------------------------
    {
    /*err code*/    "01",
    /*err msg*/     "Text Overflow",
    /*cause*/       "All available space for BASIC statements and system commands has been used.\n"
                    "Shorten and/or chain program by using COM statements, and continue.  The\n"
                    "compiler automatically removes the current and highest-numbered statement. ",
    /*action*/      nullptr,
    /*example*/     ":10 FOR I = 1 TO 10\n"
                    ":20 LET X = SIN(I)\n"
                    ":30 NEXT I\n"
                    "    ....\n"
                    "    ....\n"
                    "    ....\n"
                    ":820 IF Z = A-B THEN 900\n"
                    "^ERR 01\n"
                    "(the number of characters in the program exceeded\n"
                    "the available space in memory for program text\n"
                    "when line 820 was entered).  User must shorten or\n"
                    "segment program.",
    /*correction*/  nullptr,
    },

// -------------------------------------------------------------------------
    {
    /*err code*/    "02",
    /*err msg*/     "Table Overflow",
    /*cause*/       "All available space for the program, internal tables and variables has been\n"
                    "filled (see \"Internal Storage,\" Section I). When ERR02 occurs, all non-common\n"
                    "variables are cleared.",
    /*action*/      "Examine program for:\n"
                    "1) excessive DIM, COM statements.\n"
                    "2) subroutines not terminated by RETURN or RETURN CLEAR,\n"
                    "    improper exits from FOR/NEXT loops.\n"
                    "Suggestion: Insert an END statement as the first line in the program and\n"
                    "execute the routine. If END = value appears, the error is probably case 2);\n"
                    "otherwise, case 1). ",
    /*example*/     ":10 DIM A(19), B(10,10), C(10,10)\n"
                    "RUN\n"
                    "^ERR 02\n"
                    "(the space available for variable tables was\n"
                    "exceeded) user must reduce program and variable\n"
                    "storage requirements or change program logic.",
    /*correction*/  nullptr,
    },

// -------------------------------------------------------------------------
    {
    /*err code*/    "03",
    /*err msg*/     "Math Error",
    /*cause*/       "1.  EXPONENT OVERFLOW. The exponent of the calculated value was < -99 or > 99.\n"
                    "     (+, - *, /, ^, TAN, EXP).\n"
                    "2.  DIVISION BY ZERO.\n"
                    "3.  NEGATIVE OR ZERO LOG FUNCTION ARGUMENT.\n"
                    "4.  NEGATIVE SQR FUNCTION ARGUMENT.\n"
                    "5.  INVALID EXPONENTIATION. An exponentiation, (X^Y) was attempted where\n"
                    "     X was negative and Y was not an integer, producing an imaginary result,\n"
                    "     or X and Y were both zero.\n"
                    "6.  ILLEGAL SIN, COS, OR TAN ARGUMENT. The function argument exceeds\n"
                    "     2*pi x 10^11 radians.",
    /*action*/      "Correct the program or program data.",
    /*example*/     "PRINT (2E+64) / (2E-41)\n"
                    "          ^ERR 03\n"
                    "(exponent overflow)",
    /*correction*/  nullptr,
    },

// -------------------------------------------------------------------------
    {
    /*err code*/    "04",
    /*err msg*/     "Missing Left Parenthesis",
    /*cause*/       "A left parenthesis ( ( ) was expected.",
    /*action*/      "Correct statement text.",
    /*example*/     ":10 DEF FNA V) = SIN(3*V-1)\n"
                    "            ^ERR 04",
    /*correction*/  ":10 DEF FNA(V) = SIN(3*V-1)",
    },

// -------------------------------------------------------------------------
    {
    /*err code*/    "05",
    /*err msg*/     "Missing Right Parenthesis",
    /*cause*/       "A right ( ) ) parenthesis was expected.",
    /*action*/      "Correct statement text.",
    /*example*/     ":10 Y = INT(1.2^5\n"
                    "                 ^ERR 05",
    /*correction*/  ":10 Y = INT(1.2^5)",
    },

// -------------------------------------------------------------------------
    {
    /*err code*/    "06",
    /*err msg*/     "Missing Equals Sign",
    /*cause*/       "An equals sign (=) was expected.",
    /*action*/      "Correct statement text.",
    /*example*/     ":10 DEFFNC(V) - V + 2\n"
                    "              ^ERR 06",
    /*correction*/  ":10 DEFFNC(V) = V+2",
    },

// -------------------------------------------------------------------------
    {
    /*err code*/    "07",
    /*err msg*/     "Missing Quotation Marks",
    /*cause*/       "Quotation marks were expected.",
    /*action*/      "Reenter the DATASAVE OPEN statement correctly.",
    /*example*/     ":DATASAVE OPEN TTTT\"\n"
                    "               ^ERR 07",
    /*correction*/  ":DATASAVE OPEN \"TTTT\"",
    },

// -------------------------------------------------------------------------
    {
    /*err code*/    "08",
    /*err msg*/     "Undefined FN Function",
    /*cause*/       "An undefined FN function was referenced.",
    /*action*/      "Correct program to define or reference the function correctly.",
    /*example*/     ":10 X=FNC(2)\n"
                    ":20 PRINT \"X\";X\n"
                    ":30 END\n"
                    ":RUN\n"
                    "10 X=FNC(2)\n"
                    "      ^ERR 08",
    /*correction*/  ":05 DEFFNC(V)=COS(2*V)",
    },

// -------------------------------------------------------------------------
    {
    /*err code*/    "09",
    /*err msg*/     "Illegal FN Usage",
    /*cause*/       "More than five levels of nesting were encountered when evaluating an FN\n"
                    "function.",
    /*action*/      "Reduce the number of nested functions.",
    /*example*/     ":10 DEF FN1(X)=1+X      :DEF FN2(X)=1+FN1(X)\n"
                    ":20 DEF FN3(X)=1+FN2(X) :DEF FN4(X)=1+FN3(X)\n"
                    ":30 DEF FN5(X)=1+FN4(X) :DEF FN6(X)=1+FN5(X)\n"
                    ":40 PRINT FN6(2)\n"
                    ":RUN\n"
                    "10 DEF FN1(X)=1+X :DEF FN2(X)=1+FN1(X)\n"
                    "             ^ERR 09",
    /*correction*/  ":40 PRINT 1+FN5(2)",
    },

// -------------------------------------------------------------------------
    {
    /*err code*/    "10",
    /*err msg*/     "Incomplete Statement",
    /*cause*/       "The end of the statement was expected.",
    /*action*/      "Complete the statement text.",
    /*example*/     ":10 PRINT X\"\n"
                    "           ^ERR 10",
    /*correction*/  ":10 PRINT \"X\"\n"
                    "      OR\n"
                    ":10 PRINT X",
    },

// -------------------------------------------------------------------------
    {
    /*err code*/    "11",
    /*err msg*/     "Missing Line Number or Continue Illegal",
    /*cause*/       "The line number is missing or a referenced line number is undefined; or the\n"
                    "user is attempting to continue program execution after one of the following\n"
                    "conditions: A text or table overflow error, a new variable has been entered, a\n"
                    "CLEAR command has been entered, the user program text has been modified, or the\n"
                    "RESET key has been pressed. ",
    /*action*/      "Correct statement text.",
    /*example*/     ":10 GOSUB 200\n"
                    "          ^ERR 11",
    /*correction*/  ":10 GOSUB 100",
    },

// -------------------------------------------------------------------------
    {
    /*err code*/    "12",
    /*err msg*/     "Missing Statement Text",
    /*cause*/       "The required statement text is missing (THEN, STEP, etc.).",
    /*action*/      "Correct statement text.",
    /*example*/     ":10 IF I=12*X,45\n"
                    "             ^ERR 12",
    /*correction*/  ":10 IF I=12*X THEN 45",
    },

// -------------------------------------------------------------------------
    {
    /*err code*/    "13",
    /*err msg*/     "Missing or Illegal Integer",
    /*cause*/       "A positive integer was expected or an integer was found which exceeded the\n"
                    "allowed limit.",
    /*action*/      "Correct statement text.",
    /*example*/     ":10 COM D(P)\n"
                    "          ^ERR 13 ",
    /*correction*/  ":10 COM D(8)",
    },

// -------------------------------------------------------------------------
    {
    /*err code*/    "14",
    /*err msg*/     "Missing Relation Operator",
    /*cause*/       "A relational operator ( <, =, >, <=, >=, <> ) was expected.",
    /*action*/      "Correct statement text.",
    /*example*/     ":10 IF A-B THEN 100\n"
                    "           ^ERR 14",
    /*correction*/  ":10 IF A=B THEN 100 ",
    },

// -------------------------------------------------------------------------
    {
    /*err code*/    "15",
    /*err msg*/     "Missing Expression",
    /*cause*/       "A variable, or number, or a function was expected.",
    /*action*/      "Correct statement text.",
    /*example*/     ":10 FOR I=, TO 2\n"
                    "          ^ERR 15",
    /*correction*/  ":10 FOR I=1 TO 2",
    },

// -------------------------------------------------------------------------
    {
    /*err code*/    "16",
    /*err msg*/     "Missing Scalar Variable",
    /*cause*/       "A scalar variable was expected.",
    /*action*/      "Correct statement text.",
    /*example*/     ":10 FOR A(3)=1 TO 2\n"
                    "          ^ERR 16 ",
    /*correction*/  ":10 FOR B=I TO 2",
    },

// -------------------------------------------------------------------------
    {
    /*err code*/    "17",
    /*err msg*/     "Missing Array Element or Array",
    /*cause*/       "An array variable was expected.",
    /*action*/      "Correct statement text.",
    /*example*/     ":10 DIM A2\n"
                    "          ^ERR 17",
    /*correction*/  ":10 DIM A(2)",
    },

// -------------------------------------------------------------------------
    {
    /*err code*/    "18",
    /*err msg*/     "Illegal Value for Array Dimension",
    /*cause*/       "The value exceeds the allowable limit. For example, a dimension is greater than\n"
                    "255 or an array variable subscript exceeds the defined dimension, or an array\n"
                    "contains more than 4,096 elements.",
    /*action*/      "Correct the program.",
    /*example*/     ":10 DIM A(2,3)\n"
                    ":20 A(1,4) = 1\n"
                    ":RUN\n"
                    "20 A(1,4) = 1\n"
                    "       ^ERR 18",
    /*correction*/  ":10 DIM A(2,4)",
    },

// -------------------------------------------------------------------------
    {
    /*err code*/    "19",
    /*err msg*/     "Missing Number",
    /*cause*/       "A number was expected.",
    /*action*/      "Correct statement text.",
    /*example*/     ":10 DATA +\n"
                    "          ^ERR 19",
    /*correction*/  ":10 DATA +1",
    },

// -------------------------------------------------------------------------
    {
    /*err code*/    "20",
    /*err msg*/     "Illegal Number Format",
    /*cause*/       "The form of a number is illegal.",
    /*action*/      "Correct statement text.",
    /*example*/     ":10 A=12345678.234567\n"
                    "                     ^ERR 20\n"
                    "(More than 13 digits of mantissa) ",
    /*correction*/  ":10 A=12345678.23456",
    },

// -------------------------------------------------------------------------
    {
    /*err code*/    "21",
    /*err msg*/     "Missing Letter or Digit",
    /*cause*/       "A letter or digit was expected.",
    /*action*/      "Correct statement text.",
    /*example*/     ":10 DEF FN.(X)=X^5-1\n"
                    "          ^ERR 21",
    /*correction*/  ":10 DEF FN1(X)=X^5-1",
    },

// -------------------------------------------------------------------------
    {
    /*err code*/    "22",
    /*err msg*/     "Undefined Array Variable or Array Element",
    /*cause*/       "An array variable which was not defined properly in a DIM or COM statement is\n"
                    "referenced in the program. (An array variable was either not defined in a DIM\n"
                    "or COM statement or has been referenced as both a one-dimensional and a\n"
                    "two-dimensional array, or has been changed during execution (CLEAR V to correct\n"
                    "the latter).) ",
    /*action*/      "Correct statement text.",
    /*example*/     ":10 A(2,2) = 123\n"
                    ":RUN\n"
                    "10 A(2,2) = 123\n"
                    "    ^ERR 22",
    /*correction*/  ":1 DIM A(4,4)",
    },

// -------------------------------------------------------------------------
    {
    /*err code*/    "23",
    /*err msg*/     "No Program Statements",
    /*cause*/       "A RUN command was entered but there are no program statements.",
    /*action*/      "Enter program statements.",
    /*example*/     ":RUN\n"
                    "^ERR 23",
    /*correction*/  nullptr,
    },

// -------------------------------------------------------------------------
    {
    /*err code*/    "24",
    /*err msg*/     "Illegal Immediate Mode Statement",
    /*cause*/       "An illegal verb or transfer in an Immediate Mode statement was encountered.",
    /*action*/      "Re-enter a corrected Immediate Mode statement.",
    /*example*/     "IF A = I THEN 100\n"
                    "              ^ERR 24",
    /*correction*/  nullptr,
    },

// -------------------------------------------------------------------------
    {
    /*err code*/    "25",
    /*err msg*/     "Illegal GOSUB/RETURN Usage",
    /*cause*/       "There is no companion GOSUB statement for a RETURN statement, or a branch was\n"
                    "made into the middle of a subroutine.",
    /*action*/      "Correct the program.",
    /*example*/     ":10 FOR I=1 TO 20\n"
                    ":20 X=I*SIN(I*4)\n"
                    ":25 GOTO 100\n"
                    ":30 NEXT I: END\n"
                    ":100 PRINT \"X=\";X\n"
                    ":110 RETURN\n"
                    ":RUN\n"
                    "X=-.7568025\n"
                    "110 RETURN\n"
                    "           ^ERR 25",
    /*correction*/  ":25 GOSUB 100",
    },

// -------------------------------------------------------------------------
    {
    /*err code*/    "26",
    /*err msg*/     "Illegal FOR/NEXT Usage",
    /*cause*/       "There is no companion FOR statement for a NEXT statement, or a branch was made\n"
                    "into the middle of a FOR/NEXT loop.",
    /*action*/      "Correct the program.",
    /*example*/     ":10 PRINT \"1=\";I\n"
                    ":20 NEXT I\n"
                    ":30 END\n"
                    ":RUN\n"
                    "I=0\n"
                    "20 NEXT I\n"
                    "         ^ERR 26",
    /*correction*/  ":5 FOR I=1 TO 10",
    },

// -------------------------------------------------------------------------
    {
    /*err code*/    "27",
    /*err msg*/     "Insufficient Data",
    /*cause*/       "There are not enough data values to satisfy READ statement requirements.",
    /*action*/      "Correct program to supply additional data.",
    /*example*/     ":10 DATA 2\n"
                    ":20 READ X,Y\n"
                    ":30 END\n"
                    ":RUN\n"
                    "20 READ X,Y\n"
                    "           ^ERR 27",
    /*correction*/  ":11 DATA 3",
    },

// -------------------------------------------------------------------------
    {
    /*err code*/    "28",
    /*err msg*/     "Data Reference Beyond Limits",
    /*cause*/       "The data reference in a RESTORE statement is beyond the existing data limits.",
    /*action*/      "Correct the RESTORE statement.",
    /*example*/     ":10 DATA 1,2,3\n"
                    ":20 READ X,Y,Z\n"
                    ":30 RESTORE 5\n"
                    ":90 END\n"
                    ":RUN\n"
                    "30 RESTORE 5\n"
                    "            ^ERR 28",
    /*correction*/  ":30 RESTORE 2",
    },

// -------------------------------------------------------------------------
    {
    /*err code*/    "29",
    /*err msg*/     "Illegal Data Format ",
    /*cause*/       "The value entered as requested by an INPUT statement is in an illegal format.",
    /*action*/      "Reenter data in the correct format starting with erroneous number or terminate\n"
                    "run with the RESET key and run again.",
    /*example*/     ":10 INPUT X,Y\n"
                    ":90 END\n"
                    ":RUN\n"
                    ":INPUT\n"
                    "?1A,2E-30\n"
                    "  ^ERR 29",
    /*correction*/  "?12,2E-30",
    },

// -------------------------------------------------------------------------
    {
    /*err code*/    "30",
    /*err msg*/     "Illegal Common Assignment",
    /*cause*/       "A COM statement was preceded by a non-common variable definition.",
    /*action*/      "Correct program, making all COM statements the first numbered lines.",
    /*example*/     ":10 A=1 :B=2\n"
                    ":20 COM A,B\n"
                    ":99 END\n"
                    ":RUN\n"
                    "20 COM A,B\n"
                    "        ^ERR 30",
    /*correction*/  ":10[CR/LF-EXECUTE]\n"
                    ":30 A=1:B=2",
    },

// -------------------------------------------------------------------------
    {
    /*err code*/    "31",
    /*err msg*/     "Illegal Line Number",
    /*cause*/       "The 'statement number' key was pressed producing a'line number greater than\n"
                    "9999; or in renumbering a program with the RENUMBER command a line number was\n"
                    "generated which was greater than 9999.",
    /*action*/      "Correct the program.",
    /*example*/     ":9995 PRINT X,Y\n"
                    ":[STMT NUMBER Key]\n"
                    "^ERR 31",
    /*correction*/  nullptr,
    },

// -------------------------------------------------------------------------
    {
    /*err code*/    "33",
    /*err msg*/     "Missing HEX Digit",
    /*cause*/       "A digit or a letter from A to F was expected.",
    /*action*/      "Correct the program text.",
    /*example*/     ":10 SELECT PRINT 00P\n"
                    "                    ^ERR 33",
    /*correction*/  ":10 SELECT PRINT 005",
    },

// -------------------------------------------------------------------------
    {
    /*err code*/    "34",
    /*err msg*/     "Tape Read Error",
    /*cause*/       "The system was unable to read the next record on the tape; the tape is\n"
                    "positioned after the bad record after attempting to read the bad record ten\n"
                    "times.",
    /*action*/      nullptr,
    /*example*/     nullptr,
    /*correction*/  nullptr,
    },

// -------------------------------------------------------------------------
    {
    /*err code*/    "35",
    /*err msg*/     "Missing Comma or Semicolon",
    /*cause*/       "A comma or semicolon was expected.",
    /*action*/      "Correct statement text.",
    /*example*/     ":10 DATASAVE #2 X,Y,Z\n"
                    "               ^ERR 35",
    /*correction*/  ":10 DATASAVE #2,X,Y,Z",
    },

// -------------------------------------------------------------------------
    {
    /*err code*/    "36",
    /*err msg*/     "Illegal Image Statement",
    /*cause*/       "No format (e.g. #.##) in image statement.",
    /*action*/      "Correct the Image Statement.",
    /*example*/     ":10 PRINTUSING 20, 1.23\n"
                    ":20% AMOUNT =\n"
                    ":RUN\n"
                    ":10 PRINTUSING 20,1.23\n"
                    "                  ^ERR 36",
    /*correction*/  ":20% AMOUNT = #####",
    },

// -------------------------------------------------------------------------
    {
    /*err code*/    "37",
    /*err msg*/     "Statement Not an Image Statement",
    /*cause*/       "The statement referenced by the PRINTUSING statement is not an Image statement.",
    /*action*/      "Correct either the PRINTUSING or the Image statement.",
    /*example*/     ":10 PRINTUSING 20,X\n"
                    ":20 PRINT X\n"
                    ":RUN\n"
                    ":10 PRINTUSING 20,X\n"
                    "                    ^ERR 37",
    /*correction*/  ":20% AMOUNT = $#,###.##",
    },

// -------------------------------------------------------------------------
    {
    /*err code*/    "38",
    /*err msg*/     "Illegal Floating Point Format",
    /*cause*/       "Fewer than 4 up arrows were specified in the floating point format in an image\n"
                    "statement.",
    /*action*/      "Correct the Image statement.",
    /*example*/     ":10 % ##.##^^^\n"
                    "      ^ERR 38",
    /*correction*/  ":10 % ##.##^^^^",
    },

// -------------------------------------------------------------------------
    {
    /*err code*/    "39",
    /*err msg*/     "Missing Literal String",
    /*cause*/       "A literal string was expected.",
    /*action*/      "Correct the text.",
    /*example*/     ":10 READ A5\n"
                    ":20 DATA 123\n"
                    ":RUN\n"
                    "20 DATA 123\n"
                    "        ^ERR 39",
    /*correction*/  "20 DATA \"123\"",
    },

// -------------------------------------------------------------------------
    {
    /*err code*/    "40",
    /*err msg*/     "Missing Alphanumeric Variable",
    /*cause*/       "An alphanumeric variable was expected.",
    /*action*/      "Correct the statement text.",
    /*example*/     ":10 A$, X = \"JOHN\"\n"
                    "        ^ERR 40",
    /*correction*/  ":10 A$, X$ = \"JOHN\"",
    },

// -------------------------------------------------------------------------
    {
    /*err code*/    "41",
    /*err msg*/     "Illegal STR( Arguments",
    /*cause*/       "The STR( function arguments exceed the maximum length of the alpha variable.",
    /*action*/      nullptr,
    /*example*/     ":10 B$ = STR(A$, 10, 8)\n"
                    "                      ^ERR 41",
    /*correction*/  ":10 B$ = STR(A$, 10, 6)",
    },

// -------------------------------------------------------------------------
    {
    /*err code*/    "42",
    /*err msg*/     "File Name Too Long",
    /*cause*/       "The program name specified is too long (a maximum of 8 characters is allowed).",
    /*action*/      "Correct the program text.",
    /*example*/     ":SAVE \"PROGRAM#1\"\n"
                    "                 ^ERR 42",
    /*correction*/  ":SAVE \"PROGRAM1\"",
    },

// -------------------------------------------------------------------------
    {
    /*err code*/    "43",
    /*err msg*/     "Wrong Variable Type",
    /*cause*/       "During a DATALOAD operation a numeric (or alphanumeric) value was expected but\n"
                    "an alphanumeric (or numeric) value was read.",
    /*action*/      "Correct the program or make sure proper tape is mounted.",
    /*example*/     ":DATALOAD X, Y\n"
                    "          ^ERR 43",
    /*correction*/  ":DATALOAD X$, Y$",
    },

// -------------------------------------------------------------------------
    {
    /*err code*/    "44",
    /*err msg*/     "Program Protected",
    /*cause*/       "A program loaded was protected and, hence, cannot be SAVED or LISTED.",
    /*action*/      "Execute a CLEAR command to remove protect mode; any program in memory is\n"
                    "cleared.",
    /*example*/     nullptr,
    /*correction*/  nullptr,
    },

// -------------------------------------------------------------------------
    {
    /*err code*/    "45",
    /*err msg*/     "Program Line Too Long",
    /*cause*/       "A statement line may not exceed 192 keystrokes.",
    /*action*/      "Shorten the line being entered.",
    /*example*/     nullptr,
    /*correction*/  nullptr,
    },

// -------------------------------------------------------------------------
    {
    /*err code*/    "46",
    /*err msg*/     "New Starting Statement Number Too Low",
    /*cause*/       "The new starting statement number in a RENUMBER command is not greater than the\n"
                    "next lowest statement number.",
    /*action*/      "Reenter the RENUMBER command correctly.",
    /*example*/     "50 REM - PROGRAM 1\n"
                    "62 PRINT X, Y\n"
                    "73 GOSUB 500\n"
                    ":\n"
                    ":RENUMBER 62, 20, 5\n"
                    "   ^ERR 46",
    /*correction*/  ":RENUMBER 62, 60, 5",
    },

// -------------------------------------------------------------------------
    {
    /*err code*/    "47",
    /*err msg*/     "Illegal Or Undefined Device Specification",
    /*cause*/       "The #f file specification in a program statement is undefined.",
    /*action*/      "Define the specified file number with a SELECT statement.",
    /*example*/     ":SAVE #2\n"
                    "        ^ERR 47",
    /*correction*/  ":SELECT #2 10A\n"
                    ":SAVE #2",
    },

// -------------------------------------------------------------------------
    {
    /*err code*/    "48",
    /*err msg*/     "Undefined Keyboard Function",
    /*cause*/       "There is no DEFFN' in a user's program corresponding to the Special Function\n"
                    "key pressed.",
    /*action*/      "Correct the program.",
    /*example*/     ": [Special Function Key #2]\n"
                    "^ERR 48",
    /*correction*/  nullptr,
    },

// -------------------------------------------------------------------------
    {
    /*err code*/    "49",
    /*err msg*/     "End of Tape",
    /*cause*/       "The end of tape was encountered during a tape operation.",
    /*action*/      "Correct the program, make sure the tape is correctly positioned or, if loading\n"
                    "a program or datafile by name, be sure you have mounted the correct tape.",
    /*example*/     "100 DATALOAD X, Y, Z\n"
                    "             ^ERR 49",
    /*correction*/  nullptr,
    },

// -------------------------------------------------------------------------
    {
    /*err code*/    "50",
    /*err msg*/     "Protected Tape",
    /*cause*/       "A tape operation is attempting to write on a tape cassette that has been\n"
                    "protected (by opening tab on bottom of cassette).",
    /*action*/      "Mount another cassette or \"unprotect\" the tape cassette by covering the hole on\n"
                    "the bottom of the cassette with the tab or tape.",
    /*example*/     "SAVE/103\n"
                    "       ^ERR 50 ",
    /*correction*/  nullptr,
    },

// -------------------------------------------------------------------------
    {
    /*err code*/    "51",
    /*err msg*/     "Illegal Statement",
    /*cause*/       "The statement input is not a legal BASIC statement.",
    /*action*/      "Do not use this statement.",
    /*example*/     nullptr,
    /*correction*/  nullptr,
    },

// -------------------------------------------------------------------------
    {
    /*err code*/    "52",
    /*err msg*/     "Expected Data (Nonheader) Record",
    /*cause*/       "A DATALOAD operation was attempted but the device was not positioned at a data\n"
                    "record.",
    /*action*/      "Make sure the correct device is positioned correctly.",
    /*example*/     nullptr,
    /*correction*/  nullptr,
    },

// -------------------------------------------------------------------------
    {
    /*err code*/    "53",
    /*err msg*/     "Illegal Use of HEX Function",
    /*cause*/       "The HEX( function is being used incorrectly. The HEX function may not be used\n"
                    "in a PRINTUSING statement. ",
    /*action*/      "Do not use HEX function in this situation.",
    /*example*/     ":10 PRINTUSING 200, HEX(F4F5)\n"
                    "                    ^ERR 53",
    /*correction*/  ":10 A$ = HEX(F4F5)\n"
                    ":20 PRINTUSING 200,A$",
    },

// -------------------------------------------------------------------------
    {
    /*err code*/    "54",
    /*err msg*/     "Illegal Plot Argument",
    /*cause*/       "An argument in the PLOT statement is illegal.",
    /*action*/      "Correct the PLOT statement.",
    /*example*/     "100 PLOT <5,,H>\n"
                    "              ^ERR 54",
    /*correction*/  "100 PLOT <5,,C>",
    },

// -------------------------------------------------------------------------
    {
    /*err code*/    "55",
    /*err msg*/     "Illegal BT Argument",
    /*cause*/       "An argument in a DATALOAD BT or DATASAVE BT statement is illegal.",
    /*action*/      "Correct the statement in error.",
    /*example*/     "100 DATALOAD BT (M=50) A$\n"
                    "                 ^ERR 55",
    /*correction*/  "100 DATALOAD BT (N=50) A$",
    },

// -------------------------------------------------------------------------
    {
    /*err code*/    "56",
    /*err msg*/     "Number Exceeds Image Format",
    /*cause*/       "The value of the number being packed or converted is greater than the number of\n"
                    "integer digits provided for in the PACK or CONVERT Image.",
    /*action*/      "Change the Image specification.",
    /*example*/     "100 PACK (##) A$ FROM 1234\n"
                    "                          ^ERR 56",
    /*correction*/  "100 PACK (####) A$ FROM 1234",
    },

// -------------------------------------------------------------------------
    {
    /*err code*/    "57",
    /*err msg*/     "Value Not Between 0 and 32767",
    /*cause*/       "Illegal value specified; value is negative or greater than 32767. (The System\n"
                    "2200 cannot store a sector address greater than 32767 and cannot handle certain\n"
                    "MAT arrays with addresses outside this range.) ",
    /*action*/      "Correct the program statement in error.",
    /*example*/     "100 DATASAVE DAF (42000,X) A,B,C\n"
                    "                       ^ERR 57",
    /*correction*/  "100 DATASAVE DAF (4200,X) A,B,C",
    },

// -------------------------------------------------------------------------
    {
    /*err code*/    "58",
    /*err msg*/     "Expected Data Record",
    /*cause*/       "A program record or header record was read when a data record was expected.",
    /*action*/      "Correct the program.",
    /*example*/     "100 DATALOAD DAF(0,X) A,B,C\n"
                    "                      ^ERR 58",
    /*correction*/  nullptr,
    },

// -------------------------------------------------------------------------
    {
    /*err code*/    "59",
    /*err msg*/     "Illegal Alpha Variable For Sector Address",
    /*cause*/       "Alphanumeric receiver for the next available address in the disk DA instruction\n"
                    "is not at least 2 bytes long or MAT locator array too small.",
    /*action*/      "Dimension the alpha variable to be at least two bytes (characters) long.",
    /*example*/     "10 DIM A$1\n"
                    "100 DATASAVE DAR( 1, A$ ) X,Y,Z\n"
                    "                        ^ERR 59",
    /*correction*/  "10 DIM A$2",
    },

// -------------------------------------------------------------------------
    {
    /*err code*/    "60",
    /*err msg*/     "Array Too Small",
    /*cause*/       "The alphanumeric array does not contain enough space to store the block of\n"
                    "information being read from disk or tape or being packed into it. For cassette\n"
                    "tape and disk records, the array must contain at least 256 bytes (100 bytes for\n"
                    "100 byte cassette blocks). ",
    /*action*/      "Increase the size of the array.",
    /*example*/     "10 DIM A$(15)\n"
                    "20 DATALOAD BT A$()\n"
                    "                   ^ERR 60",
    /*correction*/  "10 DIM A$(16)",
    },

// -------------------------------------------------------------------------
    {
    /*err code*/    "61",
    /*err msg*/     "Transient Disk Hardware Error",
    /*cause*/       "The disk did not recognize or properly respond back to the System 2200 during\n"
                    "read or write operation in the proper amount of time.",
    /*action*/      "Run program again. If error persists, re-initialize the disk; if error still\n"
                    "persists contact Wang service personnel. ",
    /*example*/     "100 DATASAVE DCF X,Y,Z\n"
                    "                 ^ERR 61",
    /*correction*/  nullptr,
    },

// -------------------------------------------------------------------------
    {
    /*err code*/    "62",
    /*err msg*/     "File Full",
    /*cause*/       "The disk sector being addressed is not located within the catalogued specified\n"
                    "file. When writing, the file is full; for other operations, a SKIP or BACKSPACE\n"
                    "has set the sector address beyond the limits of the file.",
    /*action*/      "Correct the program.",
    /*example*/     "100 DATASAVE DCT#2, A$(), B$(), C$( )\n"
                    "                              ^ERR 62",
    /*correction*/  nullptr,
    },

// -------------------------------------------------------------------------
    {
    /*err code*/    "63",
    /*err msg*/     "Missing Alpha Array Designator",
    /*cause*/       "An alpha array designator (e.g., A$( ) ) was expected. (Block operations for\n"
                    "cassette and disk require an alpha array argument.)",
    /*action*/      "Correct the statement in error.",
    /*example*/     "100 DATALOAD BT A$\n"
                    "                  ^ERR 63",
    /*correction*/  "100 DATALOAD BT A$()",
    },

// -------------------------------------------------------------------------
    {
    /*err code*/    "64",
    /*err msg*/     "Sector Not On Disk or Disk Not Scratched",
    /*cause*/       "The disk sector being addressed is not on the disk. (Maximum legal sector\n"
                    "address depends upon the model of disk used.)",
    /*action*/      "Correct the program statement in error.",
    /*example*/     "100 MOVEEND F = 10000\n"
                    "                     ^ERR 64",
    /*correction*/  "100 MOVEEND F = 9791",
    },

// -------------------------------------------------------------------------
    {
    /*err code*/    "65",
    /*err msg*/     "Disk Hardware Malfunction",
    /*cause*/       "A disk hardware error occurred: i.e., the disk is not in file-ready position.\n"
                    "This could occur, for example, if the disk is in LOAD mode or power is not\n"
                    "turned on.",
    /*action*/      "Insure disk is turned on and properly setup for operation. Set the disk into\n"
                    "LOAD mode and then back into RUN mode, with the RUN/LOAD selection switch. The\n"
                    "check light should then go out. If error persists call your Wang Service\n"
                    "personnel. (Note, the disk must never be left in LOAD mode when running.) ",
    /*example*/     "100 DATALOAD DCF A$,B$\n"
                    "                ^ERR 65",
    /*correction*/  nullptr,
    },

// -------------------------------------------------------------------------
    {
    /*err code*/    "66",
    /*err msg*/     "Format Key Engaged",
    /*cause*/       "The disk format key is engaged. (The key should be engaged only when formatting\n"
                    "a disk.)",
    /*action*/      "Turn off the format key.",
    /*example*/     "100 DATASAVE DCF X,Y,Z\n"
                    "                ^ERR 66",
    /*correction*/  nullptr,
    },

// -------------------------------------------------------------------------
    {
    /*err code*/    "67",
    /*err msg*/     "Disk Format Error",
    /*cause*/       "A disk format error was detected on disk read or write. The disk is not\n"
                    "properly formatted. The error can be either in the medium or the hardware.",
    /*action*/      "Format the disk again; if error persists, call for Wang service.",
    /*example*/     "100 DATALOAD DCF X,Y,Z\n"
                    "                ^ERR 67",
    /*correction*/  nullptr,
    },

// -------------------------------------------------------------------------
    {
    /*err code*/    "68",
    /*err msg*/     "LRC Error",
    /*cause*/       "A disk longitudinal redundancy check error occurred when reading a sector. The\n"
                    "data may have been written incorrectly, or the System 2200/Disk Controller\n"
                    "could be malfunctioning.",
    /*action*/      "Run program again. If error persists, re-write the bad sector. If error still\n"
                    "persists, call Wang Service personnel.",
    /*example*/     "100 DATALOAD DCF A$( )\n"
                    "                ^ERR 68",
    /*correction*/  nullptr,
    },

// -------------------------------------------------------------------------
    {
    /*err code*/    "71",
    /*err msg*/     "Cannot Find Sector",
    /*cause*/       "A disk-seek error occurred; the specified sector could not be found on the\n"
                    "disk.",
    /*action*/      "Run program again. If error persists, re-initialize (reformat) the disk. If\n"
                    "error still occurs call Wang Service personnel.",
    /*example*/     "100 DATALOAD DCF A$( )\n"
                    "                ^ERR 71",
    /*correction*/  nullptr,
    },

// -------------------------------------------------------------------------
    {
    /*err code*/    "72",
    /*err msg*/     "Cyclic Read Error",
    /*cause*/       "A cyclic redundancy check disk read error occurred; the sector being addressed\n"
                    "has never been written to or was incorrectly written. This usually means the\n"
                    "disk was never initially formatted.",
    /*action*/      "Format the disk. If the disk was formatted, re-write the bad sector, or\n"
                    "reformat the disk. If error persists call Wang Service personnel.",
    /*example*/     "100 MOVEEND F = 8000\n"
                    "                    ^ERR 72",
    /*correction*/  nullptr,
    },

// -------------------------------------------------------------------------
    {
    /*err code*/    "73",
    /*err msg*/     "Illegal Altering Of A File",
    /*cause*/       "The user is attempting to rename or write over an existing scratched file, but\n"
                    "is not using the proper syntax. The scratched file name must be referenced.",
    /*action*/      "Use the proper form of the statement.",
    /*example*/     "SAVE DC F \"SAM 1\"\n"
                    "                 ^ERR 73",
    /*correction*/  R"(SAVE DCF ("SAM1") "SAM1")",
    },

// -------------------------------------------------------------------------
    {
    /*err code*/    "74",
    /*err msg*/     "Catalog End Error",
    /*cause*/       "The end of catalog area falls within the library index area or has been changed\n"
                    "by MOVEEND to fall within the area already used by the catalog; or there is no\n"
                    "room left in the catalog area to store more information.",
    /*action*/      nullptr,
    /*example*/     "SCRATCH DISK F LS=100, END=50\n"
                    "                             ^ERR 74",
    /*correction*/  "SCRATCH DISK F LS=100, END=500",
    },

// -------------------------------------------------------------------------
    {
    /*err code*/    "75",
    /*err msg*/     "Command Only (Not Programmable)",
    /*cause*/       "A command is being used within a BASIC program; Commands are not programmable.",
    /*action*/      "Do not use commands as program statements.",
    /*example*/     "10 LIST\n"
                    "        ^ERR 75",
    /*correction*/  nullptr,
    },

// -------------------------------------------------------------------------
    {
    /*err code*/    "76",
    /*err msg*/     "Missing < or > (in PLOT statement)",
    /*cause*/       "The required PLOT angle brackets are not in the PLOT statement.",
    /*action*/      "Correct the statement in error.",
    /*example*/     "100 PLOT A, B, \"*\"\n"
                    "         ^ERR 76",
    /*correction*/  "100 PLOT <A, B, \"*\">",
    },

// -------------------------------------------------------------------------
    {
    /*err code*/    "77",
    /*err msg*/     "Starting Sector Greater Than Ending Sector",
    /*cause*/       "The starting sector address specified is greater than the ending sector address\n"
                    "specified.",
    /*action*/      "Correct the statement in error.",
    /*example*/     "10 COPY FR(1000, 100)\n"
                    "                    ^ERR 77",
    /*correction*/  "10 COPY FR(100, 1000)",
    },

// -------------------------------------------------------------------------
    {
    /*err code*/    "78",
    /*err msg*/     "File Not Scratched",
    /*cause*/       "A file is being renamed that has not been scratched.",
    /*action*/      "Scratch the file before renaming it.",
    /*example*/     "SAVE DCF (\"LINREG\") \"LINREG2\"\n"
                    "                  ^ERR 78",
    /*correction*/  "SCRATCH F \"LINREG\"\n"
                    "SAVE DCF (\"LINREG\") \"LINREG2\"",
    },

// -------------------------------------------------------------------------
    {
    /*err code*/    "79",
    /*err msg*/     "File Already Catalogued",
    /*cause*/       "An attempt was made to catalogue a file with a name that already exists in the\n"
                    "catalogue index.",
    /*action*/      "Use a different name.",
    /*example*/     "SAVE DCF \"MATLIB\"\n"
                    "                 ^ERR 79",
    /*correction*/  "SAVE DCF \"MATLIB1\"",
    },

// -------------------------------------------------------------------------
    {
    /*err code*/    "80",
    /*err msg*/     "File Not In Catalog",
    /*cause*/       "The error may occur if one attempts to address a non-existing file name, to\n"
                    "load a data file as a program or open a program file as a data file.",
    /*action*/      "Make sure the correct file name is being used; make sure the proper disk is\n"
                    "mounted.",
    /*example*/     "LOAD DCR \"PRES\"\n"
                    "              ^ERR 80",
    /*correction*/  "LOAD DCF \"PRES\"",
    },

// -------------------------------------------------------------------------
    {
    /*err code*/    "81",
    /*err msg*/     "/XYY Device Specification Illegal",
    /*cause*/       "The /XYY device specification may not be used in this statement.",
    /*action*/      "Correct the statement in error.",
    /*example*/     "100 DATASAVE DC /310, X\n"
                    "                ^ERR 81",
    /*correction*/  "100 DATASAVE DC #1, X",
    },

// -------------------------------------------------------------------------
    {
    /*err code*/    "82",
    /*err msg*/     "No End Of File",
    /*cause*/       "No end of file record was recorded on file and therefore could not be found in\n"
                    "a SKIP END operation.",
    /*action*/      "Correct the file.",
    /*example*/     "100 DSKIP END\n"
                    "             ^ERR 82",
    /*correction*/  nullptr,
    },

// -------------------------------------------------------------------------
    {
    /*err code*/    "83",
    /*err msg*/     "Disk Hardware Error",
    /*cause*/       "A disk address was not properly transferred from the CPU to the disk when\n"
                    "executing MOVE or COPY.",
    /*action*/      "Run program again. If error persists, call Wang Field Service Personnel.",
    /*example*/     "COPY FR (100,500)\n"
                    "                ^ERR 83",
    /*correction*/  nullptr,
    },

// -------------------------------------------------------------------------
    {
    /*err code*/    "84",
    /*err msg*/     "Not Enough System 2200 Memory Available For MOVE or COPY",
    /*cause*/       "A 1K buffer is required in memory for MOVE or COPY operation. (i.e., 1000 bytes\n"
                    "should be available and not occupied by program and variables).",
    /*action*/      "Clear out all or part of program or program variables before MOVE or COPY.",
    /*example*/     "COPY FR(0, 9000)\n"
                    "                ^ERR 84",
    /*correction*/  nullptr,
    },

// -------------------------------------------------------------------------
    {
    /*err code*/    "85",
    /*err msg*/     "Read After Write Error",
    /*cause*/       "The comparison of read after write to a disk sector failed. The information was\n"
                    "not written properly. This is usually an error in the medium.",
    /*action*/      "Write the information again. If error persists, call Wang Field Service\n"
                    "personnel.",
    /*example*/     "100 DATASAVE DCF$ X, Y, Z\n"
                    "                 ^ERR 85",
    /*correction*/  nullptr,
    },

// -------------------------------------------------------------------------
    {
    /*err code*/    "86",
    /*err msg*/     "File Not Open",
    /*cause*/       "The file was not opened.",
    /*action*/      "Open the file before reading from it.",
    /*example*/     "100 DATALOAD DC A$\n"
                    "                ^ERR 86",
    /*correction*/  "10 DATALOAD DC OPEN F \"DATFIL\"",
    },

// -------------------------------------------------------------------------
    {
    /*err code*/    "87",
    /*err msg*/     "Common Variable Required",
    /*cause*/       "The variable in the LOAD DA statement, used to receive the sector address of\n"
                    "the next available sector after the load, is not a common variable.",
    /*action*/      "Define the variable to be common.",
    /*example*/     "10 LOAD DAR (100,L)\n"
                    "                  ^ERR 87",
    /*correction*/  "5 COM L",
    },

// -------------------------------------------------------------------------
    {
    /*err code*/    "88",
    /*err msg*/     "Library Index Full",
    /*cause*/       "There is no more room in the index for a new name.",
    /*action*/      "Scratch any unwanted files and compress the catalog using a MOVE statement\n"
                    "or mount a new disk platter.",
    /*example*/     "SAVE DCF \"PRGM\"\n"
                    "               ^ERR 88",
    /*correction*/  nullptr,
    },

// -------------------------------------------------------------------------
    {
    /*err code*/    "89",
    /*err msg*/     "Matrix Not Square",
    /*cause*/       "The dimensions of the operand in a MAT inversion or identity are not equal.",
    /*action*/      "Correct the array dimensions.",
    /*example*/     ":10 MAT A=IDN(3,4)\n"
                    ":RUN\n"
                    "10 MAT A=IDN(3,4)\n"
                    "                 ^ERR 89",
    /*correction*/  ":10 MAT A=IDN(3,3)",
    },

// -------------------------------------------------------------------------
    {
    /*err code*/    "90",
    /*err msg*/     "Matrix Operands Not Compatible",
    /*cause*/       "The dimensions of the operands in a MAT statement are not compatible; the\n"
                    "operation cannot be performed.",
    /*action*/      "Correct the dimensions of the arrays.",
    /*example*/     ":10 MAT A=CON(2,6)\n"
                    ":20 MAT B=IDN(2,2)\n"
                    ":30 MAT C=A+B\n"
                    ":RUN\n"
                    "30 MAT C=A+B\n"
                    "           ^ERR 90",
    /*correction*/  ":10 MAT A=CON(2,2)",
    },

// -------------------------------------------------------------------------
    {
    /*err code*/    "91",
    /*err msg*/     "Illegal Matrix Operand",
    /*cause*/       "The same array name appears on both sides of the equation in a MAT\n"
                    "multiplication or transposition statement.",
    /*action*/      "Correct the statement.",
    /*example*/     ":10 MAT A=A*B\n"
                    "             ^ERR 91",
    /*correction*/  ":10 MAT C=A*B",
    },

// -------------------------------------------------------------------------
    {
    /*err code*/    "92",
    /*err msg*/     "Illegal Redimensioning Of Array",
    /*cause*/       "The space required to redimension the array is greater than the space initially\n"
                    "reserved for the array.",
    /*action*/      "Reserve more space for array in DIM or CON statement.",
    /*example*/     ":10 DIM(3,4)\n"
                    ":20 MAT A=CON(5,6)\n"
                    ":RUN\n"
                    "20 MAT A=CON(5,6)\n"
                    "                ^ERR 92",
    /*correction*/  ":10 DIM A(5,6)",
    },

// -------------------------------------------------------------------------
    {
    /*err code*/    "93",
    /*err msg*/     "Singular Matrix",
    /*cause*/       "The operand in a MAT inversion statement is singular and cannot be inverted.",
    /*action*/      "Correct the program.",
    /*example*/     ":10 MAT A=ZER(3,3)\n"
                    ":20 MAT B=INV(A)\n"
                    ":RUN\n"
                    "20 MAT B=INV(A)\n"
                    "              ^ERR 93",
    /*correction*/  nullptr,
    },

// -------------------------------------------------------------------------
    {
    /*err code*/    "94",
    /*err msg*/     "Missing Asterisk",
    /*cause*/       "An asterisk (*) was expected.",
    /*action*/      "Correct statement text.",
    /*example*/     ":10 MAT C=(3)B\n"
                    "             ^ERR 94",
    /*correction*/  ":10 MAT C=(3)*B",
    },

// -------------------------------------------------------------------------
    {
    /*err code*/    "95",
    /*err msg*/     "Illegal Microcommand or Field/Delimiter Specification",
    /*cause*/       "The microcommand or field/delimiter specification used is invalid.",
    /*action*/      "Use only the microcommands and field/delimiter specifications provided.",
    /*example*/     ":RUN\n"
                    ":10 $GIO (1023, A$)\n"
                    "              ^ERR 95",
    /*correction*/  ":10 $GIO (0123, A$)",
    },

// -------------------------------------------------------------------------
    {
    /*err code*/    "96",
    /*err msg*/     "Missing Arg 3 Buffer",
    /*cause*/       "The buffer (Arg 3) of the $GIO statement was either omitted or already used by\n"
                    "another data input, data output, or data verify microcommand.",
    /*action*/      "Define the buffer if it was omitted, or separate the two data commands into\n"
                    "separate $GIO statements.",
    /*example*/     "10 $GIO/03B (A000 C640, A$) B$\n"
                    "                      ^ERR 96",
    /*correction*/  "10 $GIO/03B (A000, A1$) B1$\n"
                    "20 $GIO/03B (C640, A2$) B2$",
    },

// -------------------------------------------------------------------------
    {
    /*err code*/    "97",
    /*err msg*/     "Variable or Array Too Small",
    /*cause*/       "Not enough space reserved for the variable or array.",
    /*action*/      "Change dimensioning statement.",
    /*example*/     ":10 DIM A$6\n"
                    ":20 $GIO (0123, A$)\n"
                    ":RUN\n"
                    ":20 $GIO (0123, A$)\n"
                    "                 ^ERR 97",
    /*correction*/  ":10 DIM A$10",
    },

// -------------------------------------------------------------------------
    {
    /*err code*/    "98",
    /*err msg*/     "Illegal Array Delimiters",
    /*cause*/       "The number of bytes specified by the delimiters exceeds the number of bytes in\n"
                    "the array.",
    /*action*/      nullptr,
    /*example*/     ":10 DIM A$(3) 10, B$(4) 64\n"
                    ":20 $TRAN (A$()<10,23> ,B$() )\n"
                    ":RUN\n"
                    ":20 $TRAN (A$()<10,23> ,B$() )\n"
                    "                          ^ERR 98",
    /*correction*/  ":20 $TRAN (A$()<10,13> ,B$() )",
    },

// -------------------------------------------------------------------------
    {
    /*err code*/    "=1",
    /*err msg*/     "Missing Numeric Array Name",
    /*cause*/       "A numeric array name [ e.g., N( ) ] was expected.",
    /*action*/      "Correct the statement in error.",
    /*example*/     "100 MAT CONVERT A$() TO N()\n"
                    "                    ^ERR =1",
    /*correction*/  "100 MAT CONVERT N() TO A$()",
    },

// -------------------------------------------------------------------------
    {
    /*err code*/    "=2",
    /*err msg*/     "Array Too Large",
    /*cause*/       "The specified array contains too many elements.  For example, the number of\n"
                    "elements cannot exceed 4096.",
    /*action*/      "Correct the program.",
    /*example*/     "10 DIM A$(100,50)2, B$(100,50)2, W$(100,50)2\n"
                    "  .\n"
                    "  .\n"
                    "  .\n"
                    "100 MAT SORT A$() TO S$(), B$()\n"
                    "                              ^ERR =2",
    /*correction*/  "10 DIM A$(100,40)2, B$(100,40)2, W$(100,40)2",
    },

// -------------------------------------------------------------------------
    {
    /*err code*/    "=3",
    /*err msg*/     "Illegal Dimensions",
    /*cause*/       "The dimensions defined for the array or its element length are illegal.",
    /*action*/      "Dimension the array properly.",
    /*example*/     "10 DIM A$(63), B$(63)1, W$(63)2\n"
                    "100 MAT SORT A$() TO W$(), B$()\n"
                    "                              ^ERR =3",
    /*correction*/  "10 DIM A$(63), B$(63)2, W$(63)2",
    }

};


// #########################################################################
// ##                            BASIC-2 Errors                           ##
// #########################################################################

// The error message text is taken from "BASIC-2 Language Reference Manual",
// Appendix A (Wang 700-4080D, 6/81).  However, more error codes were added
// later, and those come from "Multiuser BASIC-2 Language Reference Manual",
// Appendix B (Wang 700-4080F, 3-91).
const std::vector<error_table_t> error_table_vp = {

    {
    // -------------------------------------------------------------------------
    /*err code*/    "A01",
    /*err msg*/     "MEMORY OVERFLOW",
    /*cause*/       "There is not enough memory free space remaining to enter the program line\n"
                    "or accommodate the defined variable. System commands (e.g., SAVE) and some\n"
                    "Immediate Mode statements still can be executed. (See Chapter 2, section 2.5\n"
                    "for a more detailed explanation of this error.)",
    /*recovery*/    "Make more space available by entering a CLEAR P, N, or V command to shorten\n"
                    "the program or reduce the number of variables defined.",
    /*dummy*/       nullptr,
    /*dummy*/       nullptr,
    },

// -------------------------------------------------------------------------
    {
    /*err code*/    "A02",
    /*err msg*/     "MEMORY OVERFLOW",
    /*cause*/       "There is not enough memory free space remaining to execute the program or\n"
                    "Immediate Mode line. Commands (e.g., SAVE) and some Immediate Mode statements\n"
                    "still can be executed. (See Chapter 2, section 2.5 for a more detailed\n"
                    "explanation of this error.)",
    /*recovery*/    "Make more space available by shortening the program or reducing the amount\n"
                    "of variable space used by executing a CLEAR P, N, or V command.",
    /*dummy*/       nullptr,
    /*dummy*/       nullptr,
    },

// -------------------------------------------------------------------------
    {
    /*err code*/    "A03",
    /*err msg*/     "MEMORY OVERFLOW",
    /*cause*/       "There is insufficient free space in memory to execute the LIST DC, MOVE, or\n"
                    "COPY statement (approximately 1K bytes of free space are required for MOVE\n"
                    "and COPY and 100 bytes for LIST DC).",
    /*recovery*/    "Make more space available by executing a CLEAR P, N, or V command.",
    /*dummy*/       nullptr,
    /*dummy*/       nullptr,
    },

// -------------------------------------------------------------------------
    {
    /*err code*/    "A04",
    /*err msg*/     "STACK OVERFLOW",
    /*cause*/       "A fixed-length system stack (the Operator Stack) has overflowed. A maximum\n"
                    "total of 64 levels of nesting for subroutines, FOR/NEXT loops, and\n"
                    "expression evaluation is permitted. Often this error occurs because the\n"
                    "program repeatedly branches out of subroutines or loops without executing\n"
                    "a terminating RETURN or NEXT statement.",
    /*recovery*/    "Correct the program text, possibly by using a RETURN CLEAR statement to\n"
                    "clear subroutine or loop information from the stacks.",
    /*dummy*/       nullptr,
    /*dummy*/       nullptr,
    },

// -------------------------------------------------------------------------
    {
    /*err code*/    "A05",
    /*err msg*/     "PROGRAM LINE TOO LONG",
    /*cause*/       "The program line being entered can not be saved in one disk sector because\n"
                    "its length exceeds 253 bytes. The line can be executed, but cannot be\n"
                    "saved on disk.",
    /*recovery*/    "Shorten the line by breaking it up into two or more smaller lines.",
    /*dummy*/       nullptr,
    /*dummy*/       nullptr,
    },

// -------------------------------------------------------------------------
    {
    /*err code*/    "A06",
    /*err msg*/     "PROGRAM PROTECTED",
    /*cause*/       "A program or program overlay loaded into memory was protected; therefore,\n"
                    "no program text in memory can be SAVEd, LISTed, or modified (except by LOAD\n"
                    "or CLEAR).",
    /*recovery*/    "Protect Mode must be deactivated with a CLEAR command. (However, executing\n"
                    "a CLEAR command also clears all memory.)",
    /*dummy*/       nullptr,
    /*dummy*/       nullptr,
    },

// -------------------------------------------------------------------------
    {
    /*err code*/    "A07",
    /*err msg*/     "ILLEGAL IMMEDIATE MODE STATEMENT",
    /*cause*/       "An attempt was made to execute an illegal statement in Immediate Mode.",
    /*recovery*/    "Delete the illegal statement and reexecute the line.",
    /*dummy*/       nullptr,
    /*dummy*/       nullptr,
    },

// -------------------------------------------------------------------------
    {
    /*err code*/    "A08",
    /*err msg*/     "STATEMENT NOT LEGAL HERE",
    /*cause*/       "The statement cannot be used in this context.",
    /*recovery*/    "Correct the program line.",
    /*dummy*/       nullptr,
    /*dummy*/       nullptr,
    },

// -------------------------------------------------------------------------
    {
    /*err code*/    "A09",
    /*err msg*/     "PROGRAM NOT RESOLVED",
    /*cause*/       "An attempt was made to execute an unresolved program.",
    /*recovery*/    "Resolve the program by running it with RUN.",
    /*dummy*/       nullptr,
    /*dummy*/       nullptr,
    },

// -------------------------------------------------------------------------
    {
    /*err code*/    "S10",
    /*err msg*/     "MISSING LEFT PARENTHESIS",
    /*cause*/       "A left parenthesis [ ( ] was expected.",
    /*recovery*/    "Correct the statement text.",
    /*dummy*/       nullptr,
    /*dummy*/       nullptr,
    },

// -------------------------------------------------------------------------
    {
    /*err code*/    "S11",
    /*err msg*/     "MISSING RIGHT PARENTHESIS",
    /*cause*/       "A right parenthesis [ ) ] was expected.",
    /*recovery*/    "Correct the statement text.",
    /*dummy*/       nullptr,
    /*dummy*/       nullptr,
    },

// -------------------------------------------------------------------------
    {
    /*err code*/    "S12",
    /*err msg*/     "MISSING EQUAL SIGN",
    /*cause*/       "An equal sign (=) was expected.",
    /*recovery*/    "Correct the statement text.",
    /*dummy*/       nullptr,
    /*dummy*/       nullptr,
    },

// -------------------------------------------------------------------------
    {
    /*err code*/    "S13",
    /*err msg*/     "MISSING COMMA",
    /*cause*/       "A comma (,) was expected.",
    /*recovery*/    "Correct the statement text.",
    /*dummy*/       nullptr,
    /*dummy*/       nullptr,
    },

// -------------------------------------------------------------------------
    {
    /*err code*/    "S14",
    /*err msg*/     "MISSING ASTERISK",
    /*cause*/       "An asterisk (*) was expected.",
    /*recovery*/    "Correct the statement text.",
    /*dummy*/       nullptr,
    /*dummy*/       nullptr,
    },

// -------------------------------------------------------------------------
    {
    /*err code*/    "S15",
    /*err msg*/     "MISSING \">\" CHARACTER",
    /*cause*/       "The required \">\" character is missing from the program statement.",
    /*recovery*/    "Correct the program statement syntax.",
    /*dummy*/       nullptr,
    /*dummy*/       nullptr,
    },

// -------------------------------------------------------------------------
    {
    /*err code*/    "S16",
    /*err msg*/     "MISSING LETTER",
    /*cause*/       "A letter was expected.",
    /*recovery*/    "Correct the statement text.",
    /*dummy*/       nullptr,
    /*dummy*/       nullptr,
    },

// -------------------------------------------------------------------------
    {
    /*err code*/    "S17",
    /*err msg*/     "MISSING HEX DIGIT",
    /*cause*/       "A digit or a letter from A to F was expected.",
    /*recovery*/    "Correct the program text.",
    /*dummy*/       nullptr,
    /*dummy*/       nullptr,
    },

// -------------------------------------------------------------------------
    {
    /*err code*/    "S18",
    /*err msg*/     "MISSING RELATIONAL OPERATOR",
    /*cause*/       "A relational operator ( <,=,>,<=,>=,<> ) was expected.",
    /*recovery*/    "Correct the statement text.",
    /*dummy*/       nullptr,
    /*dummy*/       nullptr,
    },

// -------------------------------------------------------------------------
    {
    /*err code*/    "S19",
    /*err msg*/     "MISSING REQUIRED WORD",
    /*cause*/       "A required BASIC word is missing (e.g., THEN or STEP).",
    /*recovery*/    "Correct the statement text.",
    /*dummy*/       nullptr,
    /*dummy*/       nullptr,
    },

// -------------------------------------------------------------------------
    {
    /*err code*/    "S20",
    /*err msg*/     "EXPECTED END OF STATEMENT",
    /*cause*/       "The end of the statement was expected. The statement syntax is correct up\n"
                    "to the point of the error message, but one or more following characters\n"
                    "make the statement illegal.",
    /*recovery*/    "Complete the statement text.",
    /*dummy*/       nullptr,
    /*dummy*/       nullptr,
    },

// -------------------------------------------------------------------------
    {
    /*err code*/    "S21",
    /*err msg*/     "MISSING LINE-NUMBER",
    /*cause*/       "A line-number in the program statement is missing.",
    /*recovery*/    "Correct the statement syntax.",
    /*dummy*/       nullptr,
    /*dummy*/       nullptr,
    },

// -------------------------------------------------------------------------
    {
    /*err code*/    "S22",
    /*err msg*/     "ILLEGAL PLOT ARGUMENT",
    /*cause*/       "An argument in the PLOT statement is illegal.",
    /*recovery*/    "Correct the PLOT statement.",
    /*dummy*/       nullptr,
    /*dummy*/       nullptr,
    },

// -------------------------------------------------------------------------
    {
    /*err code*/    "S23",
    /*err msg*/     "INVALID LITERAL STRING",
    /*cause*/       "A literal string was expected.  The length of the literal string must\n"
                    "be >= 1 and <= 255.",
    /*recovery*/    "Correct the invalid literal string.",
    /*dummy*/       nullptr,
    /*dummy*/       nullptr,
    },

// -------------------------------------------------------------------------
    {
    /*err code*/    "S24",
    /*err msg*/     "ILLEGAL EXPRESSION OR MISSING VARIABLE",
    /*cause*/       "The expression syntax is illegal or a variable is missing.",
    /*recovery*/    "Correct the syntax, or insert the missing variable.",
    /*dummy*/       nullptr,
    /*dummy*/       nullptr,
    },

// -------------------------------------------------------------------------
    {
    /*err code*/    "S25",
    /*err msg*/     "MISSING NUMERIC-SCALAR-VARIABLE",
    /*cause*/       "A numeric-scalar-variable was expected.",
    /*recovery*/    "Correct the statement text.",
    /*dummy*/       nullptr,
    /*dummy*/       nullptr,
    },

// -------------------------------------------------------------------------
    {
    /*err code*/    "S26",
    /*err msg*/     "MISSING ARRAY-VARIABLE",
    /*cause*/       "An array-variable was expected.",
    /*recovery*/    "Correct the statement text.",
    /*dummy*/       nullptr,
    /*dummy*/       nullptr,
    },

// -------------------------------------------------------------------------
    {
    /*err code*/    "S27",
    /*err msg*/     "MISSING NUMERIC-ARRAY",
    /*cause*/       "A numeric-array is required in the specified program statement syntax.",
    /*recovery*/    "Correct the program statement.",
    /*dummy*/       nullptr,
    /*dummy*/       nullptr,
    },

// -------------------------------------------------------------------------
    {
    /*err code*/    "S28",
    /*err msg*/     "MISSING ALPHA-ARRAY",
    /*cause*/       "An alpha-array is required in the specified program statement syntax.",
    /*recovery*/    "Correct the program statement.",
    /*dummy*/       nullptr,
    /*dummy*/       nullptr,
    },

// -------------------------------------------------------------------------
    {
    /*err code*/    "S29",
    /*err msg*/     "MISSING ALPHANUMERIC-VARIABLE",
    /*cause*/       "An alphanumeric-variable was expected.",
    /*recovery*/    "Correct the statement text.",
    /*dummy*/       nullptr,
    /*dummy*/       nullptr,
    },

// -------------------------------------------------------------------------
    {
    /*err code*/    "P31",
    /*err msg*/     "DO not matched with ENDDO",
    /*cause*/       "DO and ENDDO statements are not properly matched.",
    /*recovery*/    "Correct the statement text.",
    /*dummy*/       nullptr,
    /*dummy*/       nullptr,
    },

// -------------------------------------------------------------------------
    {
    /*err code*/    "P32",
    /*err msg*/     "START > END",
    /*cause*/       "The starting value is greater than the ending value.",
    /*recovery*/    "Correct the statement in error.",
    /*dummy*/       nullptr,
    /*dummy*/       nullptr,
    },

// -------------------------------------------------------------------------
    {
    /*err code*/    "P33",
    /*err msg*/     "LINE-NUMBER CONFLICT",
    /*cause*/       "The RENUMBER command cannot be executed. The renumbered program text must\n"
                    "fit between existing (nonrenumbered) program lines.",
    /*recovery*/    "Correct the RENUMBER command.",
    /*dummy*/       nullptr,
    /*dummy*/       nullptr,
    },

// -------------------------------------------------------------------------
    {
    /*err code*/    "P34",
    /*err msg*/     "ILLEGAL VALUE",
    /*cause*/       "The value exceeds the allowed limit.",
    /*recovery*/    "Correct the program or data.",
    /*dummy*/       nullptr,
    /*dummy*/       nullptr,
    },

// -------------------------------------------------------------------------
    {
    /*err code*/    "P35",
    /*err msg*/     "NO PROGRAM IN MEMORY",
    /*cause*/       "A RUN command was entered but there are no program statements in memory.",
    /*recovery*/    "Enter the program statements or load a program.",
    /*dummy*/       nullptr,
    /*dummy*/       nullptr,
    },

// -------------------------------------------------------------------------
    {
    /*err code*/    "P36",
    /*err msg*/     "UNDEFINED LINE-NUMBER OR ILLEGAL CONTINUE COMMAND",
    /*cause*/       "A referenced line-number is undefined, or the user is attempting to\n"
                    "CONTINUE program execution after one of the following conditions has occurred:\n"
                    "A Stack or Memory Overflow error, entry of a new variable or a CLEAR\n"
                    "command, modification of the user program text, or depressing the RESET Key.",
    /*recovery*/    "Correct the statement text, or rerun the program with RUN.",
    /*dummy*/       nullptr,
    /*dummy*/       nullptr,
    },

// -------------------------------------------------------------------------
    {
    /*err code*/    "P37",
    /*err msg*/     "UNDEFINED MARKED SUBROUTINE",
    /*cause*/       "There is no DEFFN' statement in the program corresponding to the GOSUB'\n"
                    "statement that was to be executed.",
    /*recovery*/    "Correct the program.",
    /*dummy*/       nullptr,
    /*dummy*/       nullptr,
    },

// -------------------------------------------------------------------------
    {
    /*err code*/    "P38",
    /*err msg*/     "UNDEFINED FN FUNCTION",
    /*cause*/       "An undefined FN function was referenced.",
    /*recovery*/    "Correct the program by defining the function or referencing it correctly.",
    /*dummy*/       nullptr,
    /*dummy*/       nullptr,
    },

// -------------------------------------------------------------------------
    {
    /*err code*/    "P39",
    /*err msg*/     "FN'S NESTED TOO DEEP",
    /*cause*/       "More than five levels of nesting were encountered when evaluating an\n"
                    "FN function.",
    /*recovery*/    "Reduce the number of nested functions.",
    /*dummy*/       nullptr,
    /*dummy*/       nullptr,
    },

// -------------------------------------------------------------------------
    {
    /*err code*/    "P40",
    /*err msg*/     R"(NO CORRESPONDING "FOR" FOR "NEXT" STATEMENT)",
    /*cause*/       "There is no companion FOR statement for a NEXT statement, or a branch was\n"
                    "made into the middle of a FOR/NEXT Loop.",
    /*recovery*/    "Correct the program.",
    /*dummy*/       nullptr,
    /*dummy*/       nullptr,
    },

// -------------------------------------------------------------------------
    {
    /*err code*/    "P41",
    /*err msg*/     "RETURN WITHOUT GOSUB",
    /*cause*/       "A RETURN statement was executed without first executing a GOSUB or GOSUB'\n"
                    "statement (e.g., a branch was made into the middle of a subroutine).",
    /*recovery*/    "Correct the program.",
    /*dummy*/       nullptr,
    /*dummy*/       nullptr,
    },

// -------------------------------------------------------------------------
    {
    /*err code*/    "P42",
    /*err msg*/     "ILLEGAL IMAGE",
    /*cause*/       "The image is not legal in this context. For example, the image referenced\n"
                    "by PRINTUSING does not contain a format-specification.",
    /*recovery*/    "Correct the program.",
    /*dummy*/       nullptr,
    /*dummy*/       nullptr,
    },

// -------------------------------------------------------------------------
    {
    /*err code*/    "P43",
    /*err msg*/     "ILLEGAL MATRIX OPERAND",
    /*cause*/       "The same array-name appears on both sides of the equation in a MAT\n"
                    "multiplication or MAT transposition statement.",
    /*recovery*/    "Correct the statement.",
    /*dummy*/       nullptr,
    /*dummy*/       nullptr,
    },

// -------------------------------------------------------------------------
    {
    /*err code*/    "P44",
    /*err msg*/     "MATRIX NOT SQUARE",
    /*cause*/       "The dimensions of the operand in a MAT inversion or identity are not equal.",
    /*recovery*/    "Correct the array dimensions.",
    /*dummy*/       nullptr,
    /*dummy*/       nullptr,
    },

// -------------------------------------------------------------------------
    {
    /*err code*/    "P45",
    /*err msg*/     "OPERAND DIMENSIONS NOT COMPATIBLE",
    /*cause*/       "The dimensions of the operands in a MAT statement are not compatible;\n"
                    "the operation cannot be performed.",
    /*recovery*/    "Correct the dimensions of the arrays.",
    /*dummy*/       nullptr,
    /*dummy*/       nullptr,
    },

// -------------------------------------------------------------------------
    {
    /*err code*/    "P46",
    /*err msg*/     "ILLEGAL MICROCOMMAND",
    /*cause*/       "A microcommand in the specified $GIO sequence is illegal or undefined.",
    /*recovery*/    "Use only legal or defined microcommands.",
    /*dummy*/       nullptr,
    /*dummy*/       nullptr,
    },

// -------------------------------------------------------------------------
    {
    /*err code*/    "P47",
    /*err msg*/     "MISSING BUFFER VARIABLE",
    /*cause*/       "A buffer (Arg-3) in the $GIO statement was omitted for a data input,\n"
                    "data output, or data verify microcommand.",
    /*recovery*/    "Define the buffer if it was omitted.",
    /*dummy*/       nullptr,
    /*dummy*/       nullptr,
    },

// -------------------------------------------------------------------------
    {
    /*err code*/    "P48",
    /*err msg*/     "ILLEGAL DEVICE SPECIFICATION",
    /*cause*/       "The #n file-number in a program statement is undefined, or the device-address\n"
                    "is illegal. On the MVP, the selected device is not contained in the Master\n"
                    "Device Table; the error is signalled when communication is attempted and\n"
                    "not when the SELECT statement is executed.",
    /*recovery*/    "Define the specified file-number in a SELECT statement, or correct the\n"
                    "device-address. (recoverable error)",
    /*dummy*/       nullptr,
    /*dummy*/       nullptr,
    },

// -------------------------------------------------------------------------
    {
    /*err code*/    "P49",
    /*err msg*/     "INTERRUPT TABLE FULL",
    /*cause*/       "Interrupts were defined for more than eight devices.  The maximum number\n"
                    "of devices allowed is eight.",
    /*recovery*/    "Reduce the number of interrupts.",
    /*dummy*/       nullptr,
    /*dummy*/       nullptr,
    },

// -------------------------------------------------------------------------
    {
    /*err code*/    "P50",
    /*err msg*/     "ILLEGAL ARRAY DIMENSIONS OR VARIABLE LENGTH",
    /*cause*/       "An array dimension or alpha-variable length exceeds the legal limits.\n"
                    "The limits are as follows:\n"
                    "    one-dimensional array: 1 <= dimension < 65536\n"
                    "    two-dimensional array: 1 <= dimension < 256\n"
                    "    alpha-variable length: 1 <= length < 125",
    /*recovery*/    "Correct the dimension or variable length.",
    /*dummy*/       nullptr,
    /*dummy*/       nullptr,
    },

// -------------------------------------------------------------------------
    {
    /*err code*/    "P51",
    /*err msg*/     "VARIABLE OR VALUE TOO SHORT",
    /*cause*/       "The length of the variable or value is too small for the specified operation.",
    /*recovery*/    "Correct the program.",
    /*dummy*/       nullptr,
    /*dummy*/       nullptr,
    },

// -------------------------------------------------------------------------
    {
    /*err code*/    "P52",
    /*err msg*/     "VARIABLE OR VALUE TOO LONG",
    /*cause*/       "The length of the variable or value is too long for the specified operation.",
    /*recovery*/    "Correct the statement or command.",
    /*dummy*/       nullptr,
    /*dummy*/       nullptr,
    },

// -------------------------------------------------------------------------
    {
    /*err code*/    "P53",
    /*err msg*/     "NONCOMMON VARIABLES ALREADY DEFINED",
    /*cause*/       "A COM statement was preceded by a noncommon variable definition.",
    /*recovery*/    "Correct the program by making all COM statements the first-numbered lines,\n"
                    "or clear the noncommon variables with a CLEAR N command.",
    /*dummy*/       nullptr,
    /*dummy*/       nullptr,
    },

// -------------------------------------------------------------------------
    {
    /*err code*/    "P54",
    /*err msg*/     "COMMON VARIABLE REQUIRED",
    /*cause*/       "The variable in the LOAD DA statement (used to receive the sector address\n"
                    "of the next available sector after the load) or the variable containing\n"
                    "the program name(s) in a multiple-file LOAD command is not a common variable.",
    /*recovery*/    "Redefine the variable to be common.",
    /*dummy*/       nullptr,
    /*dummy*/       nullptr,
    },

// -------------------------------------------------------------------------
    {
    /*err code*/    "P55",
    /*err msg*/     "UNDEFINED VARIABLE (PROGRAM NOT RESOLVED)",
    /*cause*/       "An array which was not defined properly in a DIM or COM statement is\n"
                    "referenced in the program, or a variable has been encountered which was not\n"
                    "defined because the program was not resolved (e.g., a Special Function Key\n"
                    "was used to initiate program execution, but the program was never RUN).",
    /*recovery*/    "Correct the text or RUN the program.",
    /*dummy*/       nullptr,
    /*dummy*/       nullptr,
    },

// -------------------------------------------------------------------------
    {
    /*err code*/    "P56",
    /*err msg*/     "ILLEGAL SUBSCRIPTS",
    /*cause*/       "The variable subscripts exceed the defined array dimensions or the\n"
                    "dimensions of the variable, which were defined in a DIM or COM statement,\n"
                    "do not agree with the array definition.",
    /*recovery*/    "Change the variable subscripts or the variable definition in a\n"
                    "DIM or COM statement.",
    /*dummy*/       nullptr,
    /*dummy*/       nullptr,
    },

// -------------------------------------------------------------------------
    {
    /*err code*/    "P57",
    /*err msg*/     "ILLEGAL STR ARGUMENTS",
    /*cause*/       "The STR function arguments exceed the maximum defined length of the\n"
                    "alpha-variable.",
    /*recovery*/    "Correct the STR arguments, or redefine the alpha-variable.",
    /*dummy*/       nullptr,
    /*dummy*/       nullptr,
    },

// -------------------------------------------------------------------------
    {
    /*err code*/    "P58",
    /*err msg*/     "ILLEGAL FIELD/DELIMITER SPECIFICATION",
    /*cause*/       "The field or delimiter specification in a $PACK or $UNPACK statement\n"
                    "is illegal.",
    /*recovery*/    "Correct the illegal field or delimiter specification.",
    /*dummy*/       nullptr,
    /*dummy*/       nullptr,
    },

// -------------------------------------------------------------------------
    {
    /*err code*/    "P59",
    /*err msg*/     "ILLEGAL REDIMENSION",
    /*cause*/       "The space required to redimension the array is greater than the space\n"
                    "initially reserved for the array.",
    /*recovery*/    "Reserve more space for the array in the initial DIM or COM statement,\n"
                    "or redimension the array to fit in the available space.",
    /*dummy*/       nullptr,
    /*dummy*/       nullptr,
    },

// -------------------------------------------------------------------------
    {
    /*err code*/    "C60",
    /*err msg*/     "UNDERFLOW",
    /*cause*/       "The absolute value of the calculated result was less than 1E-99 but\n"
                    "greater than zero.",
    /*recovery*/    "Correct the program or the data. Underflow errors can be suppressed by\n"
                    "executing SELECT ERROR > 60; a default value of zero will be used.",
    /*dummy*/       nullptr,
    /*dummy*/       nullptr,
    },

// -------------------------------------------------------------------------
    {
    /*err code*/    "C61",
    /*err msg*/     "OVERFLOW",
    /*cause*/       "The absolute value of the calculated result was greater than 9.999999999999E+99.",
    /*recovery*/    "Correct the program or the data. Overflow errors can be suppressed by\n"
                    "executing SELECT ERROR > 61; a default value of ±9.999999999999E+99 will be used.",
    /*dummy*/       nullptr,
    /*dummy*/       nullptr,
    },

// -------------------------------------------------------------------------
    {
    /*err code*/    "C62",
    /*err msg*/     "DIVISION BY ZERO",
    /*cause*/       "Division by a value of zero is a mathematically undefined operation.",
    /*recovery*/    "Correct the program or the data. A division-by-zero error can be\n"
                    "suppressed by executing SELECT ERROR > 62; a default value of\n"
#ifdef __WXMAC__
                    "+-"
#else
                    "\xb1"
#endif
                    "9.999999999999E+99 will be used.",
    /*dummy*/       nullptr,
    /*dummy*/       nullptr,
    },

// -------------------------------------------------------------------------
    {
    /*err code*/    "C63",
    /*err msg*/     "ZERO DIVIDED BY ZERO OR ZERO ^ ZERO",
    /*cause*/       "A mathematically indeterminate operation (0/0 or 0^0) was attempted.",
    /*recovery*/    "Correct the program or the data. Errors of this type can be suppressed by\n"
                    "executing SELECT ERROR > 63; a default value of 0 will be used.",
    /*dummy*/       nullptr,
    /*dummy*/       nullptr,
    },

// -------------------------------------------------------------------------
    {
    /*err code*/    "C64",
    /*err msg*/     "ZERO RAISED TO NEGATIVE POWER",
    /*cause*/       "Zero raised to a negative power is a mathematically undefined operation.",
    /*recovery*/    "Correct the program or the data. This error can be suppressed by executing\n"
                    "SELECT ERROR > 64; a default value of 9.999999999999E+99 will be used.",
    /*dummy*/       nullptr,
    /*dummy*/       nullptr,
    },

// -------------------------------------------------------------------------
    {
    /*err code*/    "C65",
    /*err msg*/     "NEGATIVE NUMBER RAISED TO NONINTEGER POWER",
    /*cause*/       "This is a mathematically undefined operation.",
    /*recovery*/    "Correct the program or the data. This error can be suppressed by executing\n"
                    "SELECT ERROR > 65; a default value of the absolute value of the negative\n"
                    "number raised to the noninteger power will be used.",
    /*dummy*/       nullptr,
    /*dummy*/       nullptr,
    },

// -------------------------------------------------------------------------
    {
    /*err code*/    "C66",
    /*err msg*/     "SQUARE ROOT OF NEGATIVE VALUE",
    /*cause*/       "This is a mathematically undefined operation.",
    /*recovery*/    "Correct the program or the data. This error can be suppressed by executing\n"
                    "SELECT ERROR > 66; a default value of SQR(ABS(X)), where X is the negative\n"
                    "value, will be used.",
    /*dummy*/       nullptr,
    /*dummy*/       nullptr,
    },

// -------------------------------------------------------------------------
    {
    /*err code*/    "C67",
    /*err msg*/     "LOG OF ZERO",
    /*cause*/       "This is a mathematically undefined operation.",
    /*recovery*/    "Correct the program or the data. This error can be suppressed by executing\n"
                    "SELECT ERROR > 67; a default value of -9.999999999999E±99 will be used.",
    /*dummy*/       nullptr,
    /*dummy*/       nullptr,
    },

// -------------------------------------------------------------------------
    {
    /*err code*/    "C68",
    /*err msg*/     "LOG OF NEGATIVE VALUE",
    /*cause*/       "This is a mathematically undefined operation.",
    /*recovery*/    "Correct the program or the data.  This error can be suppressed by executing\n"
                    "SELECT ERROR > 68; a default value of the LOG of the absolute value of the\n"
                    "number will be used.",
    /*dummy*/       nullptr,
    /*dummy*/       nullptr,
    },

// -------------------------------------------------------------------------
    {
    /*err code*/    "C69",
    /*err msg*/     "ARGUMENT TOO LARGE",
    /*cause*/       "The absolute value of the SIN, COS, or TAN function is >= 1E+10; the\n"
                    "system cannot evaluate the function meaningfully. Or, the absolute value of\n"
                    "the ARCSIN, ARCCOS, or ARCTAN argument is > 1.0; the value of the function\n"
                    "is mathematically undefined.",
    /*recovery*/    "Correct the program or the data. This error can be suppressed by executing\n"
                    "SELECT ERROR > 69; a default value of zero will be used.",
    /*dummy*/       nullptr,
    /*dummy*/       nullptr,
    },

// -------------------------------------------------------------------------
    {
    /*err code*/    "X70",
    /*err msg*/     "INSUFFICIENT DATA",
    /*cause*/       "There are not enough DATA values to satisfy READ or RESTORE statement\n"
                    "requirements.",
    /*recovery*/    "Correct the program to supply additional DATA, or modify the READ or\n"
                    "RESTORE statement.",
    /*dummy*/       nullptr,
    /*dummy*/       nullptr,
    },

// -------------------------------------------------------------------------
    {
    /*err code*/    "X71",
    /*err msg*/     "VALUE EXCEEDS FORMAT",
    /*cause*/       "The number of integer digits in the PACK or CONVERT image specification is\n"
                    "insufficient to express the value of the number being packed or converted.",
    /*recovery*/    "Change the image specification.",
    /*dummy*/       nullptr,
    /*dummy*/       nullptr,
    },

// -------------------------------------------------------------------------
    {
    /*err code*/    "X72",
    /*err msg*/     "SINGULAR MATRIX",
    /*cause*/       "The operand in a MAT inversion statement is singular and cannot be inverted.",
    /*recovery*/    "Correct the program or the data. Inclusion of a normalized determinant\n"
                    "parameter in the MAT INV statement will eliminate this error; however, the\n"
                    "determinant must be checked by the application program following the\n"
                    "inversion.",
    /*dummy*/       nullptr,
    /*dummy*/       nullptr,
    },

// -------------------------------------------------------------------------
    {
    /*err code*/    "X73",
    /*err msg*/     "ILLEGAL INPUT DATA",
    /*cause*/       "The value entered as requested by an INPUT statement is expressed in an\n"
                    "illegal format.",
    /*recovery*/    "Reenter the data in the correct format starting with the erroneous number,\n"
                    "or terminate run with RESET and RUN again. Alternatively, LINPUT can be\n"
                    "used to enter the data, and the data can be verified within he application\n"
                    "program.",
    /*dummy*/       nullptr,
    /*dummy*/       nullptr,
    },

// -------------------------------------------------------------------------
    {
    /*err code*/    "X74",
    /*err msg*/     "WRONG VARIABLE TYPE",
    /*cause*/       "The variable type (alpha or numeric) does not agree with the data type.\n"
                    "For example, during a DATALOAD DC operation a numeric value was expected,\n"
                    "but an alphanumeric value was read.",
    /*recovery*/    "Correct the program or the data, or verify that the proper file is being\n"
                    "accessed.",
    /*dummy*/       nullptr,
    /*dummy*/       nullptr,
    },

// -------------------------------------------------------------------------
    {
    /*err code*/    "X75",
    /*err msg*/     "ILLEGAL NUMBER",
    /*cause*/       "The format of a number is illegal.",
    /*recovery*/    "Correct the number.",
    /*dummy*/       nullptr,
    /*dummy*/       nullptr,
    },

// -------------------------------------------------------------------------
    {
    /*err code*/    "X76",
    /*err msg*/     "BUFFER EXCEEDED",
    /*cause*/       "The buffer variable is too small or too large for the specified operation.",
    /*recovery*/    "Change the size of the buffer variable.",
    /*dummy*/       nullptr,
    /*dummy*/       nullptr,
    },

// -------------------------------------------------------------------------
    {
    /*err code*/    "X77",
    /*err msg*/     "INVALID PARTITION REFERENCE",
    /*cause*/       "The partition referenced by SELECT @PART or $RELEASE TERMINAL is not\n"
                    "defined, or the name specified by DEFFN @PART has already been used.",
    /*recovery*/    "Use the proper partition name; wait for the global partition to be defined.",
    /*dummy*/       nullptr,
    /*dummy*/       nullptr,
    },

// -------------------------------------------------------------------------
    {
    /*err code*/    "X78",
    /*err msg*/     "PRINT DRIVER ERROR",
    /*cause*/       "An error was detected with the print drivers.  The error also results from an\n"
                    "invalid driver table name. The error is also returned if you attempt to\n"
                    "associate more than 15 device addresses with printer driver tables or when an\n"
                    "address associated with the printer driver tables is used more than once.",
    /*recovery*/    "Use the proper partition name; wait for the global partition to be defined.",
    /*dummy*/       nullptr,
    /*dummy*/       nullptr,
    },

// -------------------------------------------------------------------------
    {
    /*err code*/    "X79",
    /*err msg*/     "INVALID PASSWORD",
    /*cause*/       "The password entered does not match the password set when the system\n"
                    "was configured with the @GENPART utility.",
    /*recovery*/    "Use the proper password.",
    /*dummy*/       nullptr,
    /*dummy*/       nullptr,
    },

// -------------------------------------------------------------------------
    {
    /*err code*/    "D80",
    /*err msg*/     "FILE NOT OPEN",
    /*cause*/       "The file was not opened.",
    /*recovery*/    "Open the file before attempting to read from it or write to it.",
    /*dummy*/       nullptr,
    /*dummy*/       nullptr,
    },

// -------------------------------------------------------------------------
    {
    /*err code*/    "D81",
    /*err msg*/     "FILE FULL",
    /*cause*/       "The file is full; no more information may be written into the file.",
    /*recovery*/    "Correct the program, or use MOVE to move the file to another platter and\n"
                    "reserve additional space for it.",
    /*dummy*/       nullptr,
    /*dummy*/       nullptr,
    },

// -------------------------------------------------------------------------
    {
    /*err code*/    "D82",
    /*err msg*/     "FILE NOT IN CATALOG",
    /*cause*/       "A nonexistent file name was specified, or an attempt was made to load a\n"
                    "data file as a program file or a program file as a data file.",
    /*recovery*/    "Make sure the correct file name is being used, the proper disk platter is\n"
                    "mounted, and the proper disk drive is being accessed.",
    /*dummy*/       nullptr,
    /*dummy*/       nullptr,
    },

// -------------------------------------------------------------------------
    {
    /*err code*/    "D83",
    /*err msg*/     "FILE ALREADY CATALOGED",
    /*cause*/       "An attempt was made to catalog a file with a name that already exists in\n"
                    "the Catalog Index.",
    /*recovery*/    "Use a different name, or catalog the file on a different platter.",
    /*dummy*/       nullptr,
    /*dummy*/       nullptr,
    },

// -------------------------------------------------------------------------
    {
    /*err code*/    "D84",
    /*err msg*/     "FILE NOT SCRATCHED",
    /*cause*/       "An attempt was made to rename or write over a file that has not been\n"
                    "scratched.",
    /*recovery*/    "Scratch the file before renaming it.",
    /*dummy*/       nullptr,
    /*dummy*/       nullptr,
    },

// -------------------------------------------------------------------------
    {
    /*err code*/    "D85",
    /*err msg*/     "CATALOG INDEX FULL",
    /*cause*/       "There is no more room in the Catalog Index for a new name.",
    /*recovery*/    "Scratch any unwanted files and compress the catalog using a MOVE statement,\n"
                    "or mount a new disk platter and create a new catalog.",
    /*dummy*/       nullptr,
    /*dummy*/       nullptr,
    },

// -------------------------------------------------------------------------
    {
    /*err code*/    "D86",
    /*err msg*/     "CATALOG END ERROR",
    /*cause*/       "The end of the Catalog Area is defined to fall within the Catalog Index,\n"
                    "or an attempt has been made to move the end of the Catalog Area to fall\n"
                    "within the area already occupied by cataloged files (with MOVE END),\n"
                    "or there is no room left in the Catalog Area to store more information.",
    /*recovery*/    "Either correct the SCRATCH DISK or MOVE END statement, increase the size\n"
                    "of the Catalog Area with MOVE END, scratch unwanted files and compress the\n"
                    "catalog with MOVE, or open a new catalog on a separate platter.",
    /*dummy*/       nullptr,
    /*dummy*/       nullptr,
    },

// -------------------------------------------------------------------------
    {
    /*err code*/    "D87",
    /*err msg*/     "NO END-OF-FILE",
    /*cause*/       "No end-of-file record was recorded in the file by using either a\n"
                    "DATASAVE DC END or a DATASAVE DA END statement and, therefore, none could\n"
                    "be found by the DSKIP END statement.",
    /*recovery*/    "Correct the file by writing an end-of-file trailer after the last data record.",
    /*dummy*/       nullptr,
    /*dummy*/       nullptr,
    },

// -------------------------------------------------------------------------
    {
    /*err code*/    "D88",
    /*err msg*/     "WRONG RECORD TYPE",
    /*cause*/       "A program record was encountered when a data record was expected, or a\n"
                    "data record was encountered when a program record was expected.",
    /*recovery*/    "Correct the program. Be sure the proper platter is mounted and be sure the\n"
                    "proper drive is being accessed.",
    /*dummy*/       nullptr,
    /*dummy*/       nullptr,
    },

// -------------------------------------------------------------------------
    {
    /*err code*/    "D89",
    /*err msg*/     "SECTOR ADDRESS BEYOND END-OF-FILE",
    /*cause*/       "The sector address being accessed by the DATALOAD DC or DATASAVE DC\n"
                    "operation is beyond the end-of-file. This error can be caused by a bad\n"
                    "disk platter.",
    /*recovery*/    "Run the program again. If the error persists, use a different platter or\n"
                    "reformat the platter. If the error still exists, contact your Wang Service\n"
                    "Representative.",
    /*dummy*/       nullptr,
    /*dummy*/       nullptr,
    },

// -------------------------------------------------------------------------
    {
    /*err code*/    "I90",
    /*err msg*/     "DISK HARDWARE ERROR",
    /*cause*/       "The disk did not respond properly to the system at the beginning of a read\n"
                    "or write operation; the read or write has not been performed.",
    /*recovery*/    "Key RESET and run the program again. If the error persists, ensure that\n"
                    "the disk unit is powered on and that all cables are properly connected.\n"
                    "If the error still occurs, contact your Wang Service Representative.",
    /*dummy*/       nullptr,
    /*dummy*/       nullptr,
    },

// -------------------------------------------------------------------------
    {
    /*err code*/    "I91",
    /*err msg*/     "DISK HARDWARE ERROR",
    /*cause*/       "A disk hardware error occurred because the disk is not in file-ready position.\n"
                    "If the disk is in LOAD mode or if the power is not turned on, for example,\n"
                    "the disk is not in file-ready position and a disk hardware error is generated.",
    /*recovery*/    "Key RESET and run the program again. If the error recurs, check to ensure\n"
                    "that the program is addressing the correct disk platter. Be sure the disk\n"
                    "is turned on, properly set up for operation, and that all cables are\n"
                    "properly connected. Set the disk into LOAD mode and then back into RUN mode\n"
                    "by using the RUN/LOAD selection switch. If the error persists, call your\n"
                    "Wang Service Representative.\n"
                    "\n"
                    "NOTE: The disk must never be left in LOAD mode for an extended period\n"
                    "of time when the power is on.",
    /*dummy*/       nullptr,
    /*dummy*/       nullptr,
    },

// -------------------------------------------------------------------------
    {
    /*err code*/    "I92",
    /*err msg*/     "TIMEOUT ERROR",
    /*cause*/       "The device did not respond to the system in the proper amount of time\n"
                    "(time-out). In the case of the disk, the read or write operation has not\n"
                    "been performed.",
    /*recovery*/    "Key RESET and run the program again. If the error persists, be sure that\n"
                    "the disk platter has been formatted. If the error still occurs, contact\n"
                    "your Wang Service Representative.",
    /*dummy*/       nullptr,
    /*dummy*/       nullptr,
    },

// -------------------------------------------------------------------------
    {
    /*err code*/    "I93",
    /*err msg*/     "FORMAT ERROR",
    /*cause*/       "A format error was detected during a disk operation. This error indicates\n"
                    "that certain sector-control information is invalid. If this error occurs\n"
                    "during a read or write operation, the platter may need to be reformatted.\n"
                    "If this error occurs during formatting, there may be a flaw on the\n"
                    "platter's surface.",
    /*recovery*/    "Format the disk platter again. If the error persists, replace the media.\n"
                    "If the error con tinues, call your Wang Service Representative.",
    /*dummy*/       nullptr,
    /*dummy*/       nullptr,
    },

// -------------------------------------------------------------------------
    {
    /*err code*/    "I94",
    /*err msg*/     "FORMAT KEY ENGAGED",
    /*cause*/       "The disk format key is engaged. The key should be engaged only when\n"
                    "formatting a disk.",
    /*recovery*/    "Turn off the format key.",
    /*dummy*/       nullptr,
    /*dummy*/       nullptr,
    },

// -------------------------------------------------------------------------
    {
    /*err code*/    "I95",
    /*err msg*/     "DEVICE ERROR",
    /*cause*/       "A device fault occurred indicating that the disk could not perform the\n"
                    "requested operation. This error may result from an attempt to write to a\n"
                    "write-protected platter.",
    /*recovery*/    "If writing, make sure the platter is not write-protected. Repeat the\n"
                    "operation. If the error persists, power the disk off and then on, and then\n"
                    "repeat the operation. If the error still occurs, call your Wang Service\n"
                    "Representative.",
    /*dummy*/       nullptr,
    /*dummy*/       nullptr,
    },

// -------------------------------------------------------------------------
    {
    /*err code*/    "I96",
    /*err msg*/     "DATA ERROR",
    /*cause*/       "For read operations, the checksum calculations (CRC or ECC) indicate that\n"
                    "the data read is incorrect. The sector read may have been written incorrectly.\n"
                    "For disk drives that perform error correction (EC C), the error correction\n"
                    "attempt was unsuccessful For write operations, the LRC calculation indicates\n"
                    "that the data sent to the disk was incorrect. The data has not been written.",
    /*recovery*/    "For read errors, rewrite the data. If read errors persist, the disk\n"
                    "platter should be reformatted. For write errors, the write operation should\n"
                    "be repeated. If write errors persist, ensure that all cable connections are\n"
                    "properly made and are tight. If either error persists, contact your Wang\n"
                    "Service Representative.",
    /*dummy*/       nullptr,
    /*dummy*/       nullptr,
    },

// -------------------------------------------------------------------------
    {
    /*err code*/    "I97",
    /*err msg*/     "LONGITUDINAL REDUNDANCY CHECK ERROR",
    /*cause*/       "A longitudinal redundancy check error occurred when reading or writing a\n"
                    "sector. Usually, this error indicates a transmission error between the disk\n"
                    "and the CPU. However, the sector being accessed may have been previously\n"
                    "written incorrectly.",
    /*recovery*/    "Run the program again. If the error persists, rewrite the flawed sector.\n"
                    "If the error still persists, call your Wang Service Representative.",
    /*dummy*/       nullptr,
    /*dummy*/       nullptr,
    },

// -------------------------------------------------------------------------
    {
    /*err code*/    "I98",
    /*err msg*/     "ILLEGAL SECTOR ADDRESS OR PLATTER NOT MOUNTED",
    /*cause*/       "The disk sector being addressed is not on the disk, or the disk platter is\n"
                    "not mounted.  (The maximum legal sector address depends upon the disk model\n"
                    "used.)",
    /*recovery*/    "Correct the program statement in error, or mount a platter in the specified\n"
                    "drive.",
    /*dummy*/       nullptr,
    /*dummy*/       nullptr,
    },

// -------------------------------------------------------------------------
    {
    /*err code*/    "I99",
    /*err msg*/     "READ-AFTER-WRITE ERROR",
    /*cause*/       "The comparison of read-after-write to a disk sector failed, indicating that\n"
                    "the information was not written properly. This error usually indicates that\n"
                    "the disk platter is defective.",
    /*recovery*/    "Write the information again. If the error persists, try a new platter;\n"
                    "if the error still per sists, call your Wang Service Representative.",
    /*dummy*/       nullptr,
    /*dummy*/       nullptr,
    }

};

// vim: ts=8:et:sw=4:smarttab
