#!/bin/bash

/sbin/devmem 0x1e780004 32 0x2070e676
/sbin/devmem 0x1e780000 32 0xDB0EFFBF
sleep 1
/sbin/devmem 0x1e780000 32 0xFB0EFFBF
exit 0
