# Purpose: template class for file handler for the wvdutil.py program
# Author:  Jim Battle
#
# Version: 1.0, 2018/09/15, JTB
#     massive restructuring of the old wvdutil code base
# Version: 1.1, 2021/06/19, JTB
#     get rid of bilingualism (aka python2 support);
#     convert to inline type hints instead of type hint pragma comments

from typing import List, Dict, Any, Tuple  # pylint: disable=unused-import

class WvdHandler_base(object):  # pylint: disable=useless-object-inheritance

    def __init__(self):
        self._errors: List[str]   = []
        self._warnings: List[str] = []
        self._firsterr: int       = 0   # which was the first sector with an error
        self._firstwarn: int      = 0   # which was the first sector with a warning

    @staticmethod
    def name() -> str:
        return 'short description'

    @staticmethod
    def nameLong() -> str:
        # optional: override with longer description if useful
        return WvdHandler_base.name()

    # return either "P "(rogram) or "D "(ata)
    @staticmethod
    def fileType() -> str:
        return 'D '

    # pylint: disable=unused-argument, no-self-use
    def checkBlocks(self, blocks: List[bytearray],
                          options: Dict[str, Any]
                   ) -> Dict[str, Any]:
        # the options dictionary can contain these keys:
        #   'sector'    = <number> -- the absolute address of the first sector
        #   'used'      = <number> -- the "used" field from the catalog, if it is known
        #   'warnlimit' = <number> -- stop when the number of warnings is exceeded
        # the return dict contains these keys:
        #   'failed' = bool      -- True if any errors or warnings
        #   'errors' = [str]     -- list of error messages
        #   'warnings' = [str]   -- list of warning messages
        #   'lastsec' = <number> -- last valid sector before giving up
        return { 'errors':0, 'warnings':0, 'lastsec':0 }

    # the bool is True if this is a terminating block
    # pylint: disable=unused-argument, no-self-use
    def listOneBlock(self, blk: bytearray,
                           options: Dict[str, Any]
                    ) -> Tuple[bool, List[str]]:
        # the options dictionary can contain these keys:
        #   'sector'    = <number> -- the absolute address of the first sector
        #   'used'      = <number> -- the "used" field from the catalog, if it is known
        #   'warnlimit' = <number> -- stop when the number of warnings is exceeded
        return (True, [])

    # if the file type doesn't have context which crosses sectors, then
    # the default method will just repeated use listOneBlock
    def listBlocks(self, blocks: List[bytearray],
                         options: Dict[str, Any]
                  ) -> List[str]:
        # same options as listOneBlock
        listing = []
        opt = dict(options)

        for offset, blk in enumerate(blocks):
            opt['secnum'] = options['sector'] + offset
            done, morelines = self.listOneBlock(blk, opt)
            listing.extend(morelines)
            if done: break

        return listing

    # utilities to be used by derived classes
    def clearErrors(self) -> None:
        self._errors    = []
        self._warnings  = []
        self._firsterr  = 0
        self._firstwarn = 0

    def error(self, secnum: int, text: str) -> None:
        if (not self._errors) or (secnum < self._firsterr):
            self._firsterr = secnum
        self._errors.append(text)

    def warning(self, secnum: int, text: str) -> None:
        if (not self._warnings) or (secnum < self._firstwarn):
            self._firstwarn = secnum
        self._warnings.append(text)

    def status(self, sec: int, options: Dict[str, Any]) -> Dict[str, Any]:
        failed = (len(self._errors) > 0) or (len(self._warnings) > options['warnlimit'])

        if self._errors:
            last_good_sector = self._firsterr-1
        elif self._warnings:
            last_good_sector = self._firstwarn-1
        else:
            last_good_sector = sec

        return { 'failed': failed,
                 'errors': self._errors,
                 'warnings': self._warnings,
                 'lastsec': last_good_sector }
