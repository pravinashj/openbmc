#!/bin/bash

/sbin/devmem 0x1e780004 32 0x0F70e676;
/sbin/devmem 0x1e780000 32 0x330EEFBF;
sleep 7;
/sbin/devmem 0x1e780000 32 0x3B0EEFBF
sleep 1;
exit 0;
