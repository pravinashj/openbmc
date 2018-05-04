#!/bin/bash

pwrstatus=$(gpioutil -n E2 --getval)

if [ $pwrstatus -eq 0 ]; then
/usr/sbin/gpioutil -n D3 --setval 0
sleep 2
/usr/sbin/gpioutil -n D3 --setval 1
fi

exit 0
