#!/bin/bash

pwrstatus=$(gpioutil -n E2 --getval)

if [ $pwrstatus -eq 1 ]; then
/usr/sbin/gpioutil -n D3 --setval 0
sleep 7
/usr/sbin/gpioutil -n D3 --setval 1
fi

sleep 1

exit 0;
