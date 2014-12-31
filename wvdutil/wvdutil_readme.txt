Jim Battle
October, 2011

Version 1.4 of wvdutil was developed using python 2.7 under Windows 7.
It should run on other platforms, but it hasn't been tested. wvdutil_1.4.zip
contains the main program, wvdutil.py, and some library routines in wvdlib.py
and wvfilelist.py.

New features in version 1.4:

    - new "scan" command to scan the list and determine where files
      and file fragments are, irrespective of the catalog, or if the
      disk has no catalog.

    - the "cat" command now sorts the output by filename by default,
      and takes optional flags to allow sorting by disk location,
      file size, and to reverse the order of any of these.

    - "list <filename>" now also allows "list <start> <end>" to list
      the contents of sectors, one sector at a time, irrespective of
      any file boundaries.

    - the "compare" command now takes an optional -v (-verbose) flag,
      which will show the first mismatching line of a BASIC program.
      the command used to only check for binary exact equality, which
      is fast and simple, but which isn't foolproof, as most sectors
      of the program have don't-care bytes after the EOB/EOD byte.

    - "check" command much more thorough about finding problems

    - more robust operation when inspecting corrupted disks and disks
      which have no catalog

    - if you modify the disk and then exit, the program will prompt to
      ask if you want to save the image before exiting.

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
   listd     - show pretty-printed program or data file as text
   meta      - report virtual disk metadata
   platter   - supply the default if a platter number isn't specified
   protect   - set program file to be saved in protected mode
   save      - write the in-memory disk image back to disk
   scan      - scan the disk and identify whole or partial program files
   unprotect - set program file to be saved in normal mode
   wp        - set or clear the write protect status of the virtual disk

