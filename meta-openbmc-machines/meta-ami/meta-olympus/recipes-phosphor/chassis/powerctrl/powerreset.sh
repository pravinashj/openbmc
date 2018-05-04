#!/bin/bash

/usr/sbin/gpioutil -n D5 --setdir out
/usr/sbin/gpioutil -n D5 --setval 0
sleep 2
/usr/sbin/gpioutil -n D5 --setval 1

exit 0
