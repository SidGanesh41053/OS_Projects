#!/bin/bash

# Create two disk images of 1MB each
dd if=/dev/zero of=disk1 bs=1M count=1
dd if=/dev/zero of=disk2 bs=1M count=1

echo "Two disks created: disk1 and disk2"