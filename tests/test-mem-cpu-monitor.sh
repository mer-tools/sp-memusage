#!/bin/sh -e
log=/tmp/mem-cpu-monitor.log

exit_cleanup ()
{
	rm -f $log
}
trap exit_cleanup EXIT

mem-cpu-monitor -i 1 --self > $log &
pid=$!
sleep 4
kill -TERM $pid
# error if no time output in log file
grep -q '^[0-9]\+:[0-9]\+:[0-9]\+ ' $log
