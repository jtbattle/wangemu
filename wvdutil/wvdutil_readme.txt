Jim Battle
June 20, 2021

Version 1.14 of wvdutil was developed under Windows 10. It should run on other
platforms, but it hasn't been tested. The previous version was "bilingual"
(ran under both python 2 and 3), but now that python 2 is officially dead,
this program now requires a python 3 environment, specifically something
like Python 3.6 or newer (due to type hinting).

wvdutil_1.14.zip contains the main program, wvdutil.py, and its supporting
modules.

If you are on a Windows machine and don't have a python interpreter installed,
the win32 executable "wvdutil.exe" can be used instead. How it works is it
creates a temporary directory and installs a self-contained python interpreter
and all the modules used by wvdutil, then executes it. On exit the temporary
directory is removed. As a result of all this trickery, there is a short
delay on start up.

Running linting tools:

    There are a few tools you can run to get feedback on code quality.
    Not all warnings have been quashed for various reasons, including
    personal preference. For instance, I'm OK with having a larger
    number of lines per function than the linter does.

    (1) pep8 wvdutil.py
    (2) pylint wvdutil.py
    (3) mypy wvdutil.py     (type checker)

New features in version 1.14:

    - the version number has jumped a lot because I've been making more
      frequent, fine-grained fixes to github and I don't always do the
      full roll-out to this readme and releasing a new .zip file, etc.
    - support for python 2 is dropped
    - the code takes advantage of that and changes type hinting to be
      inline instead of using pragma comments
    - the code takes advantage of type aliases to make parameter intent
      clearer, eg, "Sector" instead of the generic "bytearray".
    - the code intended to have a check that the name of a program file
      in the catalog should match the name specified in the program header
      sector. there was a bug which prevented that check from working.

New features in version 1.10:

    - fix a bounds check error when a sector range is one past the end of file
    - added the "label" command to inspect or set the disk label text

New features in version 1.9:

    - added LIST tokens for DATE (0xFA) and TIME (0xFB) used in late versions
      of BASIC-2

New features in version 1.8:

    - fix a crash bug under Python 2
    - handle scrambled files even if the header block doesn't indicate scrambled
    - change the way non-printable characters are escaped in listings to be
      consistent with the way the wangemu escapes such characters in its
      scripting language

New features in version 1.7:

    - wvdutil now recognizes two styles of "EDIT" file formats

    - fixed a rare crash condition

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
   label     - report or set the disk label
   list      - show program or data file as text
   listd     - show pretty-printed program or data file as text
   meta      - report virtual disk metadata
   platter   - supply the default if a platter number isn't specified
   protect   - set program file to be saved in protected mode
   save      - write the in-memory disk image back to disk
   scan      - scan the disk and identify whole or partial program files
   unprotect - set program file to be saved in normal mode
   wp        - set or clear the write protect status of the virtual disk
