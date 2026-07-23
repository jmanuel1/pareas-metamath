#!/bin/bash
#set the process name here (make sure there is only one instance)
NAME=pareas-lpg
#set the process memory threshold in limit in KiB
MEMLIMIT=4000000
while true
do
    PID=`pgrep $NAME`
    MEM=`echo 0 $(cat /proc/$PID/smaps | grep Rss | awk '{print $2}' | sed 's#^#+#') | bc`
    echo "Memory usage: $MEM"
    if [ $MEM -gt $MEMLIMIT ]; then echo "***sending sigint***"; kill -INT $PID; fi
    sleep 1
done
