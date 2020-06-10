#!/bin/bash

# RECALL THE ORDER
n=0
num_exp=50
while [ "$n" -lt "$num_exp" ]; do
  # SEQUENTIAL RUNS
  # changing population size: pop_size=10:10000, *10 steps; chromo_size fixed at 100
  ./build/seq 100 10 100 0.5 0.3
  ./build/seq 100 100 100 0.5 0.3
  ./build/seq 100 1000 100 0.5 0.3

  #./build/seq 100 10000 100 0.5 0.3
  # changing chromosomes size: pop_size fixed at 100; chromo_size=10:10000, *10 steps
  ./build/seq 100 100 10 0.5 0.3
  #./build/seq 100 100 100 0.5 0.3
  ./build/seq 100 100 1000 0.5 0.3
  #./build/seq 100 100 10000 0.5 0.3
# ------------------------------------------------------------------------------------
  # PARALLEL RUNS
  # changing population size: pop_size=10:10000, *10 steps; chromo_size fixed at 100
  ./build/par 4 100 10 100 0.5 0.3
  ./build/par 4 100 100 100 0.5 0.3
  ./build/par 4 100 1000 100 0.5 0.3

  #./build/par 4 100 10000 100 0.5 0.3
  # changing chromosomes size: pop_size fixed at 100; chromo_size=10:10000, *10 steps
  ./build/par 4 100 100 10 0.5 0.3
  #./build/par 4 100 100 100 0.5 0.3

  ./build/par 4 100 100 1000 0.5 0.3
  #./build/par 4 100 100 10000 0.5 0.3
  n=$(( n + 1 ))
done

