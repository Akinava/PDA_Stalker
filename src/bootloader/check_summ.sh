#!/usr/bin/env bash
## - Script to make a checksumm
## - usage:
## - ./check_summ.sh "hex_line"
## - example:
## - ./check_summ.sh "027ffe00dc7e"
## - output:
## - "hex_summ"

hex_line="$1"
summ=0

for (( i=0; i<${#hex_line}; i=i+2 )); do
  hex_num="${hex_line:$i:2}"
  decimal_num=$((16#$hex_num))
  summ=$(($summ+$decimal_num))
done

summ=$(($summ%256))
summ=$((256-$summ))
printf "%02X" $summ
