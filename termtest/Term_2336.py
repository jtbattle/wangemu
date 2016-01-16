import serial

class Term_2336:
    """A wrapper around pyserial to enact Wang flow controlled serial I/O"""
    # FIXME: this doesn't compress the input stream, nor does it
    #        allow the caller to manually send a FB ... sequence.

    def __init__(self, port='COM13', baudrate=19200):
        self.throttled = False   # flow control switch
        self.ser = serial.Serial(port, baudrate,
                                 bytesize=8, parity='O', stopbits=1,
                                 timeout=None, xonxoff=0, rtscts=0)
        self.inbuf = bytearray()

    def __del__(self):
        self.ser.close()

    def _poll_input(self):
        n = self.ser.inWaiting()
        while n > 0:
            ch = self.ser.read(1)
            if ch == chr(0xFA):
                if False and not self.throttled:
                    print "Flow control on!"
                self.throttled = True
            elif ch == chr(0xF8):
                if False and self.throttled:
                    print "Flow control off"
                self.throttled = False
            else:
                self.inbuf.append(ch)
            n = self.ser.inWaiting()

    # wait until it is safe to write
    # meanwhile buffer any input we receive since we need to find
    # a flow control release
    def _wait_to_write(self):
        self._poll_input()
        while self.throttled:
            self._poll_input()
        return;

    # I'm sure there is a much better way to do this
    def _hex_to_int(ch):
        if (ch >= '0') and (ch <= '9'):
            return ord(ch) - ord('0')
        if (ch >= 'A') and (ch <= 'F'):
            return ord(ch) - ord('A') + 10
        if (ch >= 'a') and (ch <= 'f'):
            return ord(ch) - ord('a') + 10
        print "Illegal hex string character:",ch
        return 0
    _hex_to_int = staticmethod(_hex_to_int)

    # convert a string of ascii hex digits to a raw byte string
    def _to_h(str):
        if len(str) % 2 == 1:
            print "Odd length hex string:",str
            return chr(0)
        s = ''
        for n in range(0, len(str), 2):
            s += chr(  16*Term_2336._hex_to_int(str[n]) +
                          Term_2336._hex_to_int(str[n+1]) )
        return s
    _to_h = staticmethod(_to_h)

    def _attribute_bytes(self, under=False, inv=False,
                               bright=False, blink=False):
        if not bright and not blink:
            byte0 = chr(0x00)
        elif bright and not blink:
            byte0 = chr(0x02)
        elif not bright and blink:
            byte0 = chr(0x04)
        else:  # bright and blink
            byte0 = chr(0x0B)

        if not inv and not under:
            byte1 = chr(0x00)
        elif inv and not under:
            byte1 = chr(0x02)
        elif not inv and under:
            byte1 = chr(0x04)
        else:  # inverse and underlined
            byte1 = chr(0x0B)

        return ''.join([chr(0x02), chr(0x04), byte0, byte1])


    # -------------------- public functions --------------------

    # return number of bytes of pending input
    def input_count(self):
        # transfer all ser input to our own buffer so we can process
        # and filter out flow control characters
        self._poll_input()
        # how many have we buffered ourselves?
        return len(self.inbuf)

    # return true if there are unread characters from the terminal
    def input_pending(self):
        return (self.input_count() > 0)

    # read up to n bytes, return it as a string
    def read(self, size=1, blocking=True):
        rv = ''
        if size < 1:
            return rv
        while 1:
            ready = self.input_count()
            take = min(size - len(rv), ready)
            if take > 0:
                rv = ''.join(chr(b) for b in self.inbuf[0:take])
                self.inbuf = self.inbuf[take:]
            if (len(rv) == size) or not blocking:
                return rv

    # read characters until we get a CR (which isn't returned)
    def readline(self, echo=True):
        rv = ''
        while True:
            ch = self.read(1)
            if (echo):
                self.send(ch)
            if ch != chr(0x0D):
                rv += ch
            else:
                self.send(chr(0x0A))
                return rv

    def getIdString(self):
        self.sendhex('0208090F')
        return self.readline(echo=False)

    # send a string to the terminal
    def send(self, s):
        for ch in s:
            self._wait_to_write()
            if (ch == chr(0xFB)):
                # must escape to get literal 0xFB
                self.ser.write(chr(0xFB))
                self._wait_to_write()
                self.ser.write(chr(0xD0))
            else:
                self.ser.write(ch)
        return self
    
    # send n null bytes
    def pad(self,n):
        for n in (0,n):
            self.send(chr(0))
        return self

    # send a string, then automatically end CR/LF
    def sendline(self, s = ''):
        self.send(s)
        self.send(chr(0x0D))
        self.send(chr(0x0A))
        return self

    # send a string of bytes expressed a hex literal string
    def sendhex(self,s):
        self.send(Term_2336._to_h(s))
        return self

    # enable main character set
    def charset1(self):
        self.sendhex('0202000F')
        return self

    # enable alternate character set
    def charset2(self):
        self.sendhex('0202020F')
        return self

    def home(self):
        self.send(chr(0x01))
        return self

    def clear(self):
        self.send(chr(0x03))
        return self

    # cursor addressing
    # the terminal didn't have an escape sequence for cursor positioning.  
    # for the VP that sucks, but the MVP/mux solution compresses repeated
    # characters, so typically moving the cursor would end up on the wire
    # as 01 (FB nn 0A) (FB nn 09), or 7 characters.
    def at(self, row, col):
        if (0 <= col <= 79) and (0 <= row <= 23):
            self.home()
            for n in xrange(0,row): self.send(chr(0x0a))
            for n in xrange(0,col): self.send(chr(0x09))
        else:
            print "at(%d,%d) is not on screen" % (col,row)
        return self

    # cursor style
    def cursor(self, s):
        if s == 'on':
            self.send(chr(0x05))
        elif s == 'off':
            self.send(chr(0x06))
        elif s == 'blink':
            self.sendhex('02050F')
        return self

    # pause terminal for N sixths of a second, 1-9 inclusive
    def pause(self, sixths=1):
        if 1 <= sixths <= 9:
            self.send(chr(0xFB))
            self.send(chr(0xC0 + sixths))
        else:
            print "Warning: pause count of %d is not valid" % sixths
        return self
        
    # set an enable text attributes
    def attrUse(self, under=False, inv=False, bright=False, blink=False):
        attr_seq = self._attribute_bytes(
                            under=under, inv=inv, bright=bright, blink=blink )
        self.send(attr_seq)
        self.send(chr(0x0E))
        return self

    # set but don't enable text attributes
    def attrSet(self, under=False, inv=False, bright=False, blink=False):
        attr_seq = self._attribute_bytes(
                            under=under, inv=inv, bright=bright, blink=blink )
        self.send(attr_seq)
        self.send(chr(0x0F))
        return self

    # turn on most recent text attribute flags until next 0F or 0D
    def attrOn(self):
        self.send(chr(0x0E))
        return self

    # turn off special text attributes
    def attrOff(self):
        self.send(chr(0x0F))
        return self

    def box(self, h, w, erase=False):
        if (h == 0) and (w == 0):
            return self # nothing to do
        if not (0 <= h <= 23) or not (0 <= w <= 79):
            print "Warning: illogical box arguments: (%d,%d)" % (h,w)
            # but it is legal; it just wraps around

        if erase:
            self.sendhex('020B0B')
        else:  # draw
            self.sendhex('020B02')

        for n in range(0,w): self.send(chr(0x09))
        if h > 0: self.send(chr(0x0B))
        for n in range(0,h-1): self.send(chr(0x0A))
        for n in range(0,w): self.send(chr(0x08))
        if h > 0: self.send(chr(0x0B))
        for n in range(0,h-1): self.send(chr(0x0C))

        self.send(chr(0x0F))  # end
        return self
