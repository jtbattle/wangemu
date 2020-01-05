---- diagnostics.wvd ----

This is a Wang BASIC diagnostic disk. It is intended for first generation
CPUs, such as the 2200T used in the WCS family. Note that to run, the disk
must not be write protected.

---- games.wvd ----

Assortment of game programs, mostly for first generation 2200s

---- gamesall.wvd ----

Assortment of game programs, mostly for first generation 2200s

---- libraries.wvd ----

This disk contains a number of small programs to solve various problems.
Some of the programs must be edited before they will run (e.g., define
FNC(X) before integrating it).

    LOAD DCF "START"
    RUN

---- more_games.wvd ----

This is a collection of GAMES that were extracted from an old hard drive.
A few are gems; many are rehashes of Creative Computing games. Some run too
fast on a BASIC-2 machine; some depend on the graphics characters from the
22x6 terminals to be playable.

---- mvpgames.wvd ----

This is a collection of games taken of of a VP machine. Not all games require
VP/MVP, but some do.  The easiest way to launch games is to do this:

   LOAD RUN "@GAMES"

(assuming the disk is in F/310, otherwise specify the disk device)

---- stuff.wvd ----

The stuff.wvd virtual disk contains a number of game programs, three
of which were contributed by Eilert Brinkmeyer of Germany.  Here are
some details about his programs, with some editing by me.

(1) RAKETEN (by ATS 6/27/75)

It has some instructions written in german.

How to play: Rockets are trying to bomb your bunker(lower right). You can
launch missiles (with unknown reach) from three launching platforms and
steer them to catch the rocket. Use keys 1 to 3 to launch missiles and '00
and '01 to steer left or right. The platforms are filled with a random
number of missiles and they are re-filled during the game.

Some other translations:
    VERSAGER   = misfire,
    ZERSTOERT  = destroyed,
    FEUERPAUSE = cease fire

This was one of our top games, have fun! Leave emulator on 'actual speed',
otherwise you won't catch any rocket... 

(2) 8DAMEN

This little program prints out all combinations of placing 8 queens
on a chessboard, without interfering each other.

(3) RATTE  (by Matthias Muth)

Solves a given maze by finding the (shortest) way from A to Z.
There are no user hints in the program, so it's necessary to explain
the usage:

    First step is to define the maze (7x7 fields). (Lines 190-430)
    A nested loop takes you from field to field where you may set
    borders using the numpad.

        Key 4 places a left border,
        Key 2 a bottom border,
        Key 6 a right border,
        Key 8 a top border.
        With key 0 you step to the next field.
        With Keys A and Z you define Start and End of the way.

    You cannot clear borders, only set them (time for a version 1.1, ...)
    You must step through all fields to continue. The maze will be
    re-printed on the screen and the rat starts to run from the field
    "A" until it finds field "Z".

    After that, you may start another run with Key '15. The rat will
    run again, remembering the ways it can't go. Doing that repeatedly,
    the rat will find the shortest way ("KUERZESTER WEG").

    With Key '14, the rat memory is cleared and it runs again.
    Key '13 clears the maze and you may define another one.
    Code at line 780 prints the maze on paper.

---- vp-boot-2.4.wvd ----

Boot disk for VP OS version 2.4

---- mvp-boot-3.4.wvd ---
---- mvp-boot-3.5.wvd ---

Boot disk for MVP OS version 3.4
Boot disk for MVP OS version 3.5

---- basbol-boot-droz05.wvd ---
---- basbol-boot-droz08.wvd ---

These are two disks from former Wang engineer Roger Droz. They contain a
pre-beta version of the never released "BASBOL" OS. Rather than running
BASIC-2, it was the next generation called BASIC-3. Not only that, but it
was possible to have BASIC-3 running in some partitions and COBOL in other
partitions!

See basic3_notes.txt for more details.
