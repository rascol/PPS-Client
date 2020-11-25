#!/bin/bash

# pps-client-stop.sh v2.0.0

PID=`pidof pps-client`

if [ -z "$PID" ] 
then
        echo "PPS-Client is not loaded."
else
        kill $PID

        echo "Terminated PPS-Client"
fi
