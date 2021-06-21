# Program: wvdTypes.py
# Purpose: type aliases for code clarity. I tried to declare this in
#          wvdlib.py but it lead to circular include dependencies
# Author:  Jim Battle

from typing import List, Dict, Any  # pylint: disable=unused-import

Sector = bytearray   # 256 bytes specifically
SectorList = List[Sector]

# This is used to hold a dict of key/value pairs of options. Some keys point
# to int values, and some point to string values.  Unfortunately, the following
# type definition doesn't work:
# Options = Dict[str, Union[int, str]]
# There is code which does stuff like:
#     start = opts['sector']
#     sec = start + offset   << error: Unsupported operand types for + ("str" and "int")
# One fix is to require each invocation to cast it to the expected type!
#     start = cast(int, opts['sector'])
# The current 'fix' is to water down the type to accept anything:
# Perhaps switching to a TypedDict later makes sense. (requires python 3.8+)
Options = Dict[str, Any]

