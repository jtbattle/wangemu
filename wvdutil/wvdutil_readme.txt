Jim Battle
October 2008

Version 1.2 of wvdutil was developed using python 2.5 under Windows XP.
It should run on other platforms, but it hasn't been tested. wvdutil_1.2.zip
contains the main program, wvdutil.py, and some library routines in wvdlib.py
and wvfilelist.py.

New features in version 1.2:

    - can operate on large disks that are compatible with intelligent
      disk controllers, namely disks with >32K sectors per platter,
      and disks with multiple addressible platters.

    - in the wvdutil shell, the output of commands can be piped to "more", eg:
           dump 0 1023 | more

    - redirection operators (">", ">>", and "|") no longer need to be
      surrounded by whitespace to be recognized.  For example, the following
      example now works as expected:
           cat>x

    - some commands now have convenience aliases:
          exit      -> quit
          cat       -> catalog, dir, ls
          protect   -> prot
          unprotect -> unprot

    - bugs eradication

To operate on "disk.wvd", launch it like this:

    python wvdutil.py disk.wvd

The first command to learn is "help".  Here it is

Usage:
    wvdutil.py <filename> [<command>[/<platter>] [<args>]] [<redirection>]

With no command, enter into command line mode, where each subsequent line
contains one of the commands, as described below.  The output can be optionally
redirected to the named logfile, either overwriting, ">" or appending, ">>".
The output can also be piped through a pager by ending with "| more".
Arguments containing spaces can be surrounded by double quote characters.

For multiplatter disks, the platter can be specified by following the command
name with a slash and the decimal platter number.  For commands where it makes
sense, "/all" can be specified to operate on all platters.

<redirection> is optional, and takes one of three forms:
   ... >   <logfile>  -- write command output to a logfile
   ... >>  <logfile>  -- append command output to a logfile
   ... | more         -- send command output through a pager

Type "help <command>" to get more detailed help on a particular command.

   cat       - display all or selected files on the disk
   check     - check the disk data structures for consistency
   compare   - Compare two disks or selected files on two disks
   dump      - show contents of file or sectors in hex and ascii
   exit      - quit program
   help      - print list of commands more details of one command
   index     - display or change the catalog index type
   list      - show program or data file as text
   meta      - report virtual disk metadata
   platter   - supply the default if a platter number isn't specified
   protect   - set program file to be saved in protected mode
   save      - write the in-memory disk image back to disk
   unprotect - set program file to be saved in normal mode
   wp        - set or clear the write protect status of the virtual disk

