### 
# @Description: 
 # @Author: Aeneas
 # @Github: https://github.com/cofepy
 # @Date: 2019-10-27 19:31:10
 # @LastEditors: Aeneas
 # @LastEditTime: 2019-10-27 19:32:00
 ###
#!/bin/bash

rm ./*.log
killall -9 alertcenter
sleep 1
./alertcenter config.json