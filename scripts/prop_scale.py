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

Keeps partition size constant and scales the number of recovery masters and
backups.
"""

from __future__ import division, print_function
from common import *
import metrics
import recovery
import subprocess

dat = open('%s/recovery/prop_scale.data' % top_path, 'w', 1)

def run(numPartitions, numBackups, disk, hostAllocationStrategy):
    args = {}
    args['numBackups'] = numBackups
    args['numPartitions'] = numPartitions
    args['objectSize'] = 1024
    args['disk'] = disk
    args['replicas'] = 3
    args['numObjects'] = 626012 * 600 // 640
    args['oldMasterArgs'] = '-m 17000'
    args['newMasterArgs'] = '-m 800'
    args['timeout'] = 180

    r = recovery.insist(**args)
    print('->', r['ns'] / 1e6, 'ms', '(run %s)' % r['run'])
    diskActiveMsPoints = [backup.backup.storageReadTicks * 1e3 /
                          backup.clockFrequency
                          for backup in r['metrics'].backups]
    segmentsPerBackup = [backup.backup.storageReadCount
                         for backup in r['metrics'].backups]
    masterRecoveryMs = [master.recoveryTicks / master.clockFrequency * 1000
                        for master in r['metrics'].masters]
    print(numPartitions, r['ns'] / 1e6,
          metrics.average(diskActiveMsPoints),
          min(diskActiveMsPoints),
          max(diskActiveMsPoints),
          (min(segmentsPerBackup) *
           sum(diskActiveMsPoints) / sum(segmentsPerBackup)),
          (max(segmentsPerBackup) *
           sum(diskActiveMsPoints) / sum(segmentsPerBackup)),
          metrics.average(masterRecoveryMs),
          min(masterRecoveryMs),
          max(masterRecoveryMs),
          file=dat)

numHosts = 35
for n in range(1, numHosts // 7 + 1):
    print('%d masters sharing hosts with 0 backups' % n)
    #run(n, 6*n, disk=1, hostAllocationStrategy=1)
    print(0, 0, file=dat)
print(file=dat)
print(file=dat)
for n in range(1, numHosts // 6 + 1):
    print('%d masters sharing hosts with 1 backup' % n)
    #run(n, 6*n, disk=1, hostAllocationStrategy=0)
    print(0, 0, file=dat)
print(file=dat)
print(file=dat)
for n in range(1, numHosts // 3 + 1):
    print('%d masters sharing hosts with 2 backups' % n)
    run(n, 6*n, disk=3, hostAllocationStrategy=0)
print(file=dat)
print(file=dat)
for n in range(1, numHosts // 3 + 1):
    print('%d masters sharing hosts with 1 RAID backup' % n)
    #run(n, 3*n, disk=4, hostAllocationStrategy=0)
    print(0, 0, file=dat)
