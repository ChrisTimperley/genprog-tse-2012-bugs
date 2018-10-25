#!/bin/bash
ulimit -c 8
test_id=$1
here_dir=$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )
inputs="${here_dir}/inputs"
executable="${here_dir}/flex"

case $test_id in
  p1) "${executable}" < "${inputs}/t1" |& diff output.t1 - && exit 0;;
  p2) "${executable}" < "${inputs}/t2" |& diff output.t2 - && exit 0;;
  p3) "${executable}" < "${inputs}/t3" |& diff output.t3 - && exit 0;;
  p4) "${executable}" < "${inputs}/t4" |& diff output.t4 - && exit 0;;
  p5) "${executable}" < "${inputs}/t5" |& diff output.t5 - && exit 0;;
  n1)
    rm -rf core.*
    "${executable}" < "${inputs}/t11"
    if [ ! -f core.* ] ; then exit 0; fi
esac
exit 1
