#!/bin/bash
# Test that the STM32 device has at least 128k flash.
# Generate 128k random data, write it, then read it back, and diff.
export BAUD=921600
dd if=/dev/urandom of=xxx bs=1024 count=128
sudo ~/stm32flash/stm32flash -S 0x08000000 -b $BAUD -w ./xxx /dev/ttyUSB0
sudo ~/stm32flash/stm32flash -S 0x08000000 -b $BAUD -r ./yyy /dev/ttyUSB0
diff -s xxx yyy
rm -f xxx yyy
sudo ~/stm32flash/stm32flash -o -b $BAUD /dev/ttyUSB0
