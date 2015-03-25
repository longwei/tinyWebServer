#/usr/local/bin/python

import sys
import os


# for param in os.environ.keys():
#   print "%s %s " % (param, os.environ[param])

value = os.environ["QUERY_STRING"]
# value2 = os.environ["BAR"]
print value

