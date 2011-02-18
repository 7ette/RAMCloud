#!/usr/bin/env python

# Copyright (c) 2011 Stanford University
#
# Permission to use, copy, modify, and distribute this software for any
# purpose with or without fee is hereby granted, provided that the above
# copyright notice and this permission notice appear in all copies.
#
# THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR(S) DISCLAIM ALL WARRANTIES
# WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
# MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL AUTHORS BE LIABLE FOR
# ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
# WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
# ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
# OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.

"""Generates data for a recovery performance graph.

Varies the number of backups feeding data to one recovery master.
"""

from __future__ import division
from common import *
import recovery
import subprocess

dat = open('%s/recovery/backup_scale.data' % top_path, 'w')

for numBackups in range(1, 7):
    args = {}
    args['numBackups'] = numBackups
    args['numPartitions'] = 1
    args['objectSize'] = 1024
    args['disk'] = '/dev/sdb1'
    args['numObjects'] = 626012 * 400 // 640
    args['oldMasterArgs'] = '-m 3000'
    while True:
        try:
            r = recovery.recover(**args)
        except subprocess.CalledProcessError, e:
            print e
        else:
            break
    print 'Recovery', r
    dat.write('%d\t%d\n' % (numBackups, r['ns']))
    dat.flush()
