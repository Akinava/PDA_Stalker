#!/usr/bin/env bash
## - Script to make a checksumm
## - usage:
## - ./build_hex.sh "hex_file_path.hex" "function1 address1 function2 address2 etc..."
## - example:
## - ./build_hex.sh boot "load_default_app 7ffe load_app_by_name 7ffc load_app_by_cluster 7ffa"
## - output:
## - "hex_summ"


line_start=":"
line_len="02"
line_flash_data_type="00"
closer=":00000001FF"
file_name=$1
func_param=$2
hex_file=${file_name}.hex
new_hex_file=${file_name}_with_addr.hex

avr-objdump -D ${file_name}.elf > ${file_name}.asm
head -n -1 $hex_file > $new_hex_file

IFS=', ' read -r -a func_data_array <<< "$func_param"
length=${#func_data_array[@]}

for (( i=0; i<$length; i=i+2 )); do
  func_name="${func_data_array[$i]}"
  func_store_addr="${func_data_array[$i+1]}"
  func_flash_addr_hex=$(grep "<${func_name}>:" ${file_name}.asm | awk '{print $1}' | cut -c5-8)
  func_flash_addr_decimal=$((16#$func_flash_addr_hex))
  func_flash_addr_word_decimal=$(($func_flash_addr_decimal/2))
  func_flash_addr_word_hex=$(printf "%02X" $func_flash_addr_word_decimal)
  func_flash_addr_word_hex_revert="${func_flash_addr_word_hex:2:2}${func_flash_addr_word_hex:0:2}"
  hex_line=${line_len}${func_store_addr}${line_flash_data_type}${func_flash_addr_word_hex_revert}
  summ=$(./check_summ.sh $hex_line)
  echo ${line_start}${hex_line}${summ} >> $new_hex_file
done

echo $closer >> $new_hex_file
mv $new_hex_file $hex_file