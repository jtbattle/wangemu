Here are a few notes on the programs in this directory.

battle_orig.w22 --

is a game written by William Woods in 1974.  It obviously is using is
using an early version of Wang BASIC, 2200A most likely.  As a result, the
KEYIN statement can't be used, which requires an odd style of gameplay.
Press the HALT/STEP key when you want to fire, then press the SF15 key to
actually shoot.  (On WangEmu, the Pause/Break key is used for HALT/STEP,
and Control-F11 is SF15).  Despite some limitations in 2200A BASIC, the
program is pretty awkwardly written, and the game is limited to just three
possible scenarios.


battle.w22 --

is my rewrite of battle_orig.w22.  In my version, which three of the ten
ships are the targets is completely random.  I've made a few other changes
to make the game more playable, such as not having to press the HALT key
then SF15 to fire -- just press any key to shoot.  This version takes
advtantage of some of the extra features in 2200T BASIC, making the code
smaller.  Note how the DEFFN'nn() feature is used exetensively to save
code; also note that these are located at the start of the program since
the program runs faster that way.


charset.w22 --

this simply prints out all the printable characters available.


factor.w22 --

Dumb program to factorize a given number.


football.w22 --

Human vs human, human vs computer, or computer vs computer football
simulation.  The game has directions.  This version is slightly different
than the version that appeared in WangEmu 1.0.  The one appearing here is
an older version, from Feb 22, 1974.


hexapawn.w22 --

This game was originally written for an HP timeshare system by R.A. Kaapke
in 1976.  It was converted to other dialects a couple times, then converted
to Wang BASIC.  It is a 3x3 chessboard with 3 pawns vs 3 pawns.  The game
is to get one of your pawns into the opponent's back row or to block any
legal move for your opponent.  The trick is that the computer starts out
knowing only the rules and no strategy.  As it plays, it learns what not
to do and so plays better.


highlow.w22 --

This is a simple game where the computer picks a number and the user
tries to guess it.


mstrmind.w22 --

This is another Creative Computing game (by Steve North) ported to the
Wang by Jim Battle.  You play vs. the computer in trying to guess which
pieces were selected by the opponent.  Note that it makes use of the
MAT SEARCH and MAT COPY command.

Also see mastermind.w22, described below.


primes.w22 --

Simple demonstration program that uses a brute force search for prime
numbers, printing them all.


randplot.w22 --

This program shows up the weakness of the Wang BASIC RND() function.
Rather than eventually filling the screen by visiting every location,
instead there is a pattern.  Run this program under Wang BASIC-2 and
the problem goes away.


randwalk.w22 --

This is a demonstration program where an asterisk is moved by nudging
it in one of four directions at each step, wrapping around thee edges.
At each step, the new cursor location is drawn by homing the cursor,
stepping down R rows and across C columns before the "*" can be drawn.
This is because the CRT, addressed as /005, forces a carriage return
after every 64 characters.


randwalk2.w22 --

This is an update of randwalk.w22.  By using output device /405, the
program can use purely relative cursor motion without having forced
linefeeds.  /4xx is normally used only for plotters.


scroll.w22 --

This demonstration program just scrolls the A-Z across the screen
by a MATPRINT statement.  Note the use of MATCOPY to speed up the
horizontal scrolling.  It wouldn't take too much more effort to
have it scroll an arbitrary message.


sieve.w22 --

A version of the Sieve of Eratosthenes prime counting program.  It was
an early 80's benchmark program that was rather inadequate but yet widely
used and reported.


sinewave.w22 --

This is a simple demonstration program that prints a vertical sine wave.


starinst.w22 --
startrek.w22 --

A Startrek clone.  (possibly by Mark Musen?)


tictac.w22 --

This is another Creative Computing game ported to Wang BASIC by Jim
Battle.  It doesn't really use any advanced features of Wang BASIC.


wumpus.w22 --

One of many variations of Hunt The Wumpus, ported to Wang BASIC by
Jim Battle.  The only thing interesting in this port is the use of
the GOSUB'1("string") routine.

-------------------------------------------------------------------------

The following four game programs were contributed by Eilert Brinkmeyer of
Germany.  Here are some details about his programs, with some editing by me.

raketen.w22 (by ATS 6/27/75) --

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

8damen.w22 --

This little program prints out all combinations of placing 8 queens
on a chessboard, without interfering each other.

ratte.w22 (by Matthias Muth) --

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

mastermind.w22 --

This program tries to guess the pattern you give it.  It is like
mstrmind.w22, except it only plays the guessing side of the game,
and is fixed to four positions of six colors.  The game directions
are in German, but it is easy enough to figure out.

