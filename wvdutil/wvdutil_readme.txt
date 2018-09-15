Jim Battle
September 15, 2018

Version 1.6 of wvdutil was developed under Windows 7, and should be compatible
with both Python 2.7 and Python 3.7.  It should run on other platforms, but it
hasn't been tested.

wvdutil_1.6.zip contains the main program, wvdutil.py, and some library
routines in wvdlib.py and wvfilelist.py.

It also contains wvdutil.exe.  This is a single-file binary which contains
a python interpreted and the wvdutil source all bundled together.  This is
useful if you don't have python already installed on your machine.  However,
it is actually slower to start up than running the python script.

Running linting tools:

    There are a few tools you can run to get feedback on code quality.
    Not all warnings have been quashed for various reasons, a big one
    being py2/3 compatibility, and another being I'm OK with having
    a larger number of lines per function than the linter does.

    (1) pep8 wvdutil.py
    (2) pylint wvdutil.py
    (3) mypy wvdutil.py     (type checker)

New features in version 1.6:

    - added ability to list and unprotect program files which were saved
      in scrambled (SAVE !) format

    - added the ability to list text files saved by the EDIT editor

    - when listing programs, tokens are expanded only in some contexts,
      ala BASIC-2. Tokens aren't expanded inside quotes, image statements,
      and REMs.

    - fixed a bug where running "<somecommand> | more" would always crash
      when using python3.  it worked fine with python2.

    - with python3, dumping data files contained "bytearray(...)" spew

    - after installing an updated version of python (3.7), pylint, and mypy,
      some new warnings were addressed.

    - the program was significantly restructured to make checking/listing
      different file formats more regular.  there is still a lot of accreted
      cruft.

New features in version 1.5:

    - codebase modified to be bilingual: runs under either Python 2.7 or
      Python (>)3.6

    - PEP 484 type annotations were added to the code.  This was done to
      make it easier to catch errors when I made the python2/3 change.
      However, it requires that your python site library requires that the
      "future" and "typing" modules be installed.
      ("pip install future" / "pip install typing")

    - many complaints of pylint were addressed, but some I don't want to
      hear about are suppressed via the new file "pylintrc" which is in the
      same directory as the other files.

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

