# Purpose: class for verifying and listing Wang EDIT text files
# Author:  Jim Battle
#
# Version: 1.0, 2018/09/15, JTB
#     massive restructuring of the old wvdutil code base

from __future__ import print_function
from typing import List, Dict, Any, Tuple  # pylint: disable=unused-import
from wvdHandler_base import WvdHandler_base

########################################################################
# class definition

class WvdHandler_edit(WvdHandler_base):

    def __init__(self):
        WvdHandler_base.__init__(self)

    @staticmethod
    def name():
        return "edit"

    @staticmethod
    def nameLong():
        return "EDIT editor source code text file"

    # EDIT files are legal data files, but with a very constrained format.
    # 1) each sector but the last is
    #     81 01 be <62 bytes>
    #           be <62 bytes>
    #           be <62 bytes>
    #           be <62 bytes> fe 00
    # 2) the last sector is a0 <255 don't care bytes>.
    # 3) the first few bytes of the first string in the first sector must start
    #    with the magic bytes 02 01, 'FILE = ', 8 filename bytes, padded on the
    #    right with spaces, and a date.  the date is of the format
    #        [0-9]{1,2}/[0-9]{1,2}/([0-9]{2}|[0-9]{4})
    # pylint: disable=too-many-branches
    def checkBlocks(self, blocks, options):
        # type: (List[bytearray], Dict[str, Any]) -> Dict[str, Any]

        if 'warnlimit' not in options: options['warnlimit'] = 0

        start = options['sector']  # sector offset of first sector of file

        self.clearErrors()
        end_byte_seen = False

        if not blocks:
            self.error(start, "this is a null file")
            return self.status(start, options)

        sec = start
        for offset, blk in enumerate(blocks):
            sec = start + offset

            if (blk[0] & 0xf0) == 0xa0:
                # file trailer record
                if offset == 0:
                    self.error(sec, "first sector was trailer record")
                if not end_byte_seen:
                    self.error(sec, "sector %d: end byte 0x03 not seen before the trailer record" % sec)
                break

            if end_byte_seen:
                self.error(sec, "sector %d: end byte \\x03 byte seen; expected trailer record next" % sec)
                break

            # start of block control byte
            if blk[0] != 0x81:
                self.error(sec, "sector %d started with unexpected control byte 0x%02x; expected 0x81" \
                                % (sec, blk[0]))
                break

            # sector within logical record sequencing byte
            if blk[1] != 0x01:
                self.error(sec, "sector %d started with unexpected sector sequence byte 0x%02x; expected 0x01" \
                                % (sec, blk[1]))
                break

            # verify that the block contains four strings of 62 bytes each
            if blk[2+0*63] != 0xbe or blk[2+1*63] != 0xbe  or \
               blk[2+2*63] != 0xbe or blk[2+3*63] != 0xbe:
                self.error(sec, "sector %d: expected to see four strings of 62 bytes each" % sec)
                break

            # the last two bytes should be fd 00, but we check only 0xfd byte
            # since the final byte shouldn't matter
            if blk[0xfe] != 0xfd:
                self.error(sec, "sector %d: end of block byte was 0x%02x (expected 0xfd)" % (sec, blk[0xfe]))
                break

            end_byte_seen = 0x03 in blk

            if offset == 0:
                # the first string contains magic bytes, the filename, and date
                line0 = blk[3+0*63:2+1*63]
                if line0[0] != 0x02 or line0[1] != 0x01:
                    self.error(sec, "sector %d: magic bytes %02x%02x found instead of 0201" \
                                    % (sec, line0[0], line0[1]))
                    break
                if line0[3:10] != b'FILE = ':
                    self.error(sec, "sector %d: first line didn't contain the filename" % sec)
                    break
                # TODO: check the date format?

            # if (error or warning count exceeds limit)
            if self._errors or (len(self._warnings) > options['warnlimit']):
                break

        return self.status(sec, options)

    # listing one block at a time probably doesn't make sense, as the lines of
    # the file are packed and run across sector boundaries
    def listOneBlock(self, blk, options):
        # type: (bytearray, Dict[str, Any]) -> Tuple[bool, List[str]]
        return (True, [])

    ########################################################################
    # given a list of file blocks, return a program file listing
    # pylint: disable=too-many-locals, too-many-branches
    def listBlocks(self, blocks, options):
        # type: (List[bytearray], Dict[str, Any]) -> List[str]

        filename = '        '  # pylint: disable=unused-variable
        listing = []
        curline = ''

        for secnum, secData in enumerate(blocks):

            if (secData[0] & 0xf0) == 0xa0:
                # trailer record
                break

            # grab the four strings
            line0 = secData[3+0*63:2+1*63]
            line1 = secData[3+1*63:2+2*63]
            line2 = secData[3+2*63:2+3*63]
            line3 = secData[3+3*63:2+4*63]

            if secnum == 0:
                # first sector should start with 0x02 0x01 {length},
                # followed by some text.  the {length} byte indicates an offset
                # to where the first asm source line begins.  that first
                # bit of text doesn't end in a carriage return, so we add it.
                # but if there is an expclit one there, we convert it to a space
                # because that is what the EDIT program does.
                if line0[0] != 0x02 or line0[1] != 0x01 or line0[2] not in range(0x16,0x1b):
                    print("relative sector 0: ",
                          "first line of file started with 0x%02x 0x%02x 0x%02x " %
                          (line0[0], line0[1], line0[2]),
                          "(expected 0x02 0x01 (0x16..0x1a))")
                    break
                if line0[3:10] != b'FILE = ':
                    print("relative sector 0: first line didn't contain the filename")
                    break
                filename = line0[10:18].decode('latin_1')  # 8 characters
                # OK, here is some hokey stuff that the EDIT program does for
                # inexplicable reasons.  my guess is that an older version of edit
                # used a slightly different format, and the later version would
                # fix up files in that old format.
                offset = line0[2]  # 0x16..0x1a
                if line0[2+offset] == 0x1e:
                    line0[2+offset] = 0x20  # convert to space
                # chomp of that weird three byte header, and split off
                # the file description section from the first actual line
                encoding = 'latin-1'
                line0 = bytearray('* ', encoding) + line0[3:3+offset] + bytearray(chr(0x1E), encoding) + line0[3+offset:]

            lines = []
            for t in (line0, line1, line2, line3):
                end_byte_seen = 0x03 in t
                if end_byte_seen:
                    idx = t.index(0x03)
                    t = t[0:idx]  # chomp 0x03 and everything after it
                    end_byte_seen = True
                lines.append(t)
                if end_byte_seen:
                    break

            # scan through all lines regardless of boundaries and assemble lines
            for line in lines:
                for byt in line:
                    if byt == 0x1E:
                        # end of line
                        listing.append(curline)
                        curline = ''
                    elif byt == 0x09:
                        # tab
                        curline += "\t"
                    elif 32 <= byt <= 128:
                        curline += chr(byt)
                    else:
                        curline += "\\x%02x" % byt

        # all blocks processed
        if curline != '':
            listing.append(curline)

        # add filename
        # listing.insert(0, "# FILENAME='%s'" % filename)
        return listing
