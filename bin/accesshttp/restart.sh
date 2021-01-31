### 
# @Description: 
 # @Author: Aeneas
 # @Github: https://github.com/cofepy
 # @Date: 2019-10-27 19:30:40
 # @LastEditors: Aeneas
 # @LastEditTime: 2019-10-27 19:30:52
 ###
 
#!/bin/bash

rm ./*.log
killall -9 accesshttp
sleep 1
./accesshttp config.json