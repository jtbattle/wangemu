from Term_2236 import *
import time

# main
term = Term_2236(port='COM13', baudrate=19200)

term.clear().charset1().cursor('on')
term.attrSet(bright=True).attrOff()

term.sendline("Hello from python")
term.sendline()

if 0:
    term.clear()
    term.attrUse(under=False)

    term.sendline("Charset #1:")
    term.send(''.join(' ' for i in range(16)))
    term.charset1()
    for n in range(16, 256):
        term.send(chr(n))
        if (n % 64) == 63:
            term.sendline()
    term.sendline();
    term.sendline();

    term.sendline("Charset #2:");
    term.send(''.join(' ' for i in range(16)))
    term.charset2()
    for n in range(16, 256):
        term.send(chr(n))
        if (n % 64) == 63:
            term.sendline()
    term.sendline();
    term.sendline();

    # this should be a solid block of white
    term.at(14,0)
    for n in range(4):
        term.sendline(''.join(chr(0xFF) for i in range(16)))

if 0:
    for n in range(1,50):
        s = "%d * %d = %d" % (n,n,n*n)
        term.sendline(s)
    time.sleep(2)
    term.sendhex('03')

if 0:
    term.send("cursor is on:")
    term.cursor('on')
    time.sleep(2)
    term.sendline()

    term.send("cursor is off:")
    term.cursor('off')
    time.sleep(2)
    term.sendline()

    term.send("cursor is blinking:")
    term.cursor('blink')
    time.sleep(6)
    term.sendline()

    term.send("cursor is on:")
    term.cursor('on')
    time.sleep(2)
    term.sendline()

if 0:
    # attribute test
    term.attrUse()
    term.sendline("Normal")
    term.attrUse(under=True)
    term.sendline("Underlined")
    term.attrUse(inv=True)
    term.sendline("Inverse")
    term.attrUse(blink=True)
    term.sendline("Blinking")
    term.attrUse(bright=True)
    term.sendline("Highlighted")

    term.attrUse(under=True,inv=True)
    term.sendline("Underlined and inverse")
    term.attrUse(under=True,blink=True)
    term.sendline("Underlined and blinking")
    term.attrUse(under=True,bright=True)
    term.send("Underlined").attrOff().send(" and ").attrOn().sendline("highlighted")
    term.attrUse(inv=True,bright=True).send("both ").attrUse(inv=True).sendline("one").attrOff()
    term.attrUse(under=True,inv=True,bright=True).send("both ").attrUse(under=True,inv=True).sendline("one").attrOff()
    term.sendline()

if 0:
    # fetch id string
    id_string = term.getIdString()
    print "id string:",id_string

if 0:
    term.send("What is your name? ")
    name = term.readline()
    term.sendline("Hi, " + name + "!")

if 0:
    # cursor positioning
    term.clear().send("home")
    term.at(5,5).send("(5,5)")
    term.at(10,10).send("(10,10)")
    term.sendline()

if 0:
    # box drawing
    term.clear()
    term.at(5,5).box(1,5).attrUse(blink=1).send(" here").attrOff()
    term.sendline()
    term.at(7,0).box(0,11).send(" horizontal")
    term.at(5,30).box(10,0).send(" vertical")
    time.sleep(1)
    term.at(5,5).box(1,5,erase=True)
    term.at(20,0)

if 0:
    # test delay command
    term.clear()
    for delay in range(1,10):
        term.send("Delay %d: " % delay)
        for n in range(0,5): term.send('.').pause(delay)
        term.sendline()

if 0:
    # draw a grid of lines on top of inverse screen
    term.clear()
    term.attrUse(inv=True)
    for row in range(1,5):
        for col in range(0,60):
            term.at(row,col).box(1,1)

if 0:
    # double check the blink frequency, duty cycle, and relative phases
    term.clear()
    term.attrUse(blink=True)
    term.sendline("This is blinking text")
    term.cursor('blink')
    time.sleep(100)

if 0:
    # Q: what happens if we try to draw a box 24 rows high?
    # A: it works; there is a 25th line just for displaying
    #    final row of box graphics (horizontal lines only)
    term.clear()
    term.at(0,0).send("line 0")
    term.at(23,0).send("line 23")
    term.home()
    term.box(24,60)
    term.at(10,10)
    term.read(1)
    # Q: what if we draw a box off the screen?
    term.clear()
    term.sendline("box horizontal wrap-around test")
    term.at(4,50).box(1,50)
    term.read(1)
    # Q: what if we draw a box wider than the screen?
    # A: horizontal movements wrap l/r without changing lines
    term.clear()
    term.sendline("wide box test")
    term.at(4,0).box(1,200)
    term.read(1)
    # Q: what if we draw a box taller than the screen?
    term.clear()
    term.sendline("tall box test")
    term.at(0,20).box(25,2)
    term.read(1)

if 1:
    # blink w/ intense test
    term.clear().attrUse()
    term.attrUse().send("Normal intensity, no blink, normal  video, no underline --> ").attrUse(blink=0, bright=0, under=0, inv=0).sendline("TEST STRING")
    term.attrUse().send("Normal intensity, no blink, reverse video, no underline --> ").attrUse(blink=0, bright=0, under=0, inv=1).sendline("TEST STRING")
    term.attrUse().send("Normal intensity, no blink, normal  video,    underline --> ").attrUse(blink=0, bright=0, under=1, inv=0).sendline("TEST STRING")
    term.attrUse().send("Normal intensity, no blink, reverse video,    underline --> ").attrUse(blink=0, bright=0, under=1, inv=1).sendline("TEST STRING")
                                                                                                         
    term.attrUse().send("High   intensity, no blink, normal  video, no underline --> ").attrUse(blink=0, bright=1, under=0, inv=0).sendline("TEST STRING")
    term.attrUse().send("High   intensity, no blink, reverse video, no underline --> ").attrUse(blink=0, bright=1, under=0, inv=1).sendline("TEST STRING")
    term.attrUse().send("High   intensity, no blink, normal  video,    underline --> ").attrUse(blink=0, bright=1, under=1, inv=0).sendline("TEST STRING")
    term.attrUse().send("High   intensity, no blink, reverse video,    underline --> ").attrUse(blink=0, bright=1, under=1, inv=1).sendline("TEST STRING")
                                                                                                         
    term.attrUse().send("Normal intensity, blinking, normal  video, no underline --> ").attrUse(blink=1, bright=0, under=0, inv=0).sendline("TEST STRING")
    term.attrUse().send("Normal intensity, blinking, reverse video, no underline --> ").attrUse(blink=1, bright=0, under=0, inv=1).sendline("TEST STRING")
    term.attrUse().send("Normal intensity, blinking, normal  video,    underline --> ").attrUse(blink=1, bright=0, under=1, inv=0).sendline("TEST STRING")
    term.attrUse().send("Normal intensity, blinking, reverse video,    underline --> ").attrUse(blink=1, bright=0, under=1, inv=1).sendline("TEST STRING")
                                                                                                         
    term.attrUse().send("High   intensity, blinking, normal  video, no underline --> ").attrUse(blink=1, bright=1, under=0, inv=0).sendline("TEST STRING")
    term.attrUse().send("High   intensity, blinking, reverse video, no underline --> ").attrUse(blink=1, bright=1, under=0, inv=1).sendline("TEST STRING")
    term.attrUse().send("High   intensity, blinking, normal  video,    underline --> ").attrUse(blink=1, bright=1, under=1, inv=0).sendline("TEST STRING")
    term.attrUse().send("High   intensity, blinking, reverse video,    underline --> ").attrUse(blink=1, bright=1, under=1, inv=1).sendline("TEST STRING")

if 0:
    # blink w/ intense test
    term.clear().attrUse()
    term.attrUse().send("Normal intensity, no blink, normal  video --> ").attrUse(bright=0, blink=0, inv=0).sendline("TEST STRING")
    term.attrUse().send("Normal intensity, no blink, reverse video --> ").attrUse(bright=0, blink=0, inv=1).sendline("TEST STRING")
    term.attrUse().send("High   intensity, no blink, normal  video --> ").attrUse(bright=1, blink=0, inv=0).sendline("TEST STRING")
    term.attrUse().send("High   intensity, no blink, reverse video --> ").attrUse(bright=1, blink=0, inv=1).sendline("TEST STRING")
    term.attrUse().send("Normal intensity, blinking, normal  video --> ").attrUse(bright=0, blink=1, inv=0).sendline("TEST STRING")
    term.attrUse().send("Normal intensity, blinking, reverse video --> ").attrUse(bright=0, blink=1, inv=1).sendline("TEST STRING")
    term.attrUse().send("High   intensity, blinking, normal  video --> ").attrUse(bright=1, blink=1, inv=0).sendline("TEST STRING")
    term.attrUse().send("High   intensity, blinking, reverse video --> ").attrUse(bright=1, blink=1, inv=1).sendline("TEST STRING")

if 0:
    # attribute vs overlay testing
    term.charset2()
    for blink in (False, True):
        for ch in (0x20, 0xFF):
            fill = ''.join(chr(ch) for n in range(72))
            for inv in (False, True):
                for bright in (False, True):
                    print "blink=%d, fill:0x%02x, inv=%d, bright=%d" % (blink,ch,inv,bright)
                    term.clear()
                    term.attrUse(inv=inv, bright=bright, blink=blink)
                    for row in range(1,5): term.sendline(fill)
                    term.at(1,10).box(0,20)  # horizontal line
                    term.at(0,2).box(7,0)    # vertical line
                    term.at(3,10)            # cursor
                    foo = term.read(1)

if 0:
    # what if a bunch of 0As are emitted?
    # does the x cursor position get reset on scroll?
    term.clear()
    term.at(0,0).send("top row")
    term.at(23,0).send("bottom row")
    term.at(0,20)
    for n in range(30):
        term.sendhex('0A')
        time.sleep(0.25)

if 0:
    # what if a bunch of 0Cs are emitted?
    # does the x cursor position get reset on scroll?  does it do reverse scroll?
    term.clear()
    term.at(0,0).send("top row")
    term.at(23,0).send("bottom row")
    term.at(10,20)
    for n in range(20):
        term.sendhex('0C')
        time.sleep(0.25)

if 0:
    term.clear()
    term.sendline("testing beep")
    while 1:
        term.sendhex('07')
        time.sleep(1)

if 0:
    # what does a blinking cursor on inverse field look like?
    term.clear()
    term.attrUse(inv=True)
    for n in range(8):
        term.sendline("                        ")
    term.at(4,8)
    while True:
        term.cursor("on")
        time.sleep(5)
        term.cursor("blink")
        time.sleep(5)
        term.cursor("off")
        time.sleep(5)

if 0:
    # verify width of cursor relative to character
    term.clear()
    term.charset2()
    term.at(1,0).sendhex('FFFFFF')
    term.at(2,1).sendhex('FF')
    term.at(3,0).sendhex('FFFFFF')
    term.at(2,1)
    time.sleep(100)

if 0:
    term.clear()
    term.sendline("READY (BASIC-2)")
    term.send(":")
    term.readline()

if 0:
    term.clear()
    term.charset2()
    term.at(10,35).attrUse(inv=False).sendhex('FFFF41FFFF').sendline()
    term.at(11,35).attrUse(inv=True).sendhex('FFFF41FFFF').sendline()
    term.at(12,35).attrUse(inv=False).sendhex('FFFF41FFFF').sendline()
    term.at(16,0)

if 0:
    term.clear()
    term.sendline("Testing cursor escape codes")
    time.sleep(5)

    if 1:
        term.sendline("Cursor is on")
        term.cursor("on")
        time.sleep(5)

    if 1:
        term.sendline("Cursor is off")
        term.cursor("off")
        time.sleep(5)

    if 1:
        term.sendline("Turn on blink")
        term.cursor("blinkon")
        time.sleep(5)

    if 0:
        term.sendline("Turn off blink")
        term.cursor("blinkoff")
        time.sleep(5)

    if 1:
        term.sendline("Cursor is on again")
        term.cursor("on")
        time.sleep(5)

# testing the bell
if 0:
    term.clear()
    term.sendline("Testing the bell")

    if 0:
        for n in range(5):
            term.sendline("One HEX(07)")
            term.sendhex('07')
            time.sleep(1)

        for n in range(5):
            term.sendline("Two HEX(07)")
            term.sendhex('0707')
            time.sleep(1)

        for n in range(5):
            term.sendline("Ten HEX(07)")
            term.sendhex('07070707070707070707')
            time.sleep(1)

        for n in range(5):
            term.sendline("Five HEX(070D)")
            term.sendhex('070D070D070D070D070D')
            time.sleep(1)

        # up to this point, they all sound pretty much the same

        # this sounds like about 50% duty cycle:
        # one second of beeping, one second of silence
        lots = '07' * 1400
        for n in range(5):
            term.sendline("Six hundred HEX(07)")
            term.sendhex(lots)
            time.sleep(1)

    # how many dead characters do we need between beeps before we
    # can hear a break between the beeps?
    # the "2008" (space, backspace) to ensure every byte is transmitted
    # and not just a run of one single character.
    # no breakup at 85, but gaps at 90.
    #    (180 chars)*(11bits/char)*(1 sec/19200 bits) = 0.103 sec
    # so, yeah, each 07 fires a 0.1 sec one-shot
    lots = '07' + '2008'*90
    term.sendline("HEX(07) followed by 180 null characters")
    for n in range(100):
        term.sendhex(lots)

    term.cursor("on")
    time.sleep(5)

    # conclusion after trying various combinations:
    #  1: turning blink on and off attribute while the cursor is off
    #     doens't turn the cursor back on
    #  2: turning the cursor on forces blink off

# character set
if 0:
    term.clear()
    term.sendline("Character set")
    for n in range(16,256):
        term.sendhex('%02x' % n)
        if n%16 == 15:
            term.sendhex('0d0a')

term.cursor('on').charset1().attrOff()
term.sendline("done!")

del term
