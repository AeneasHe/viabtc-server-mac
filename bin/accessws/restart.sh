### 
# @Description: 
 # @Author: Aeneas
 # @Github: https://github.com/cofepy
 # @Date: 2019-10-27 19:31:10
 # @LastEditors: Aeneas
 # @LastEditTime: 2019-10-27 19:33:22
 ###
#!/bin/bash

rm ./*.log
killall -s SIGQUIT accessws.exe
sleep 1
./accessws.exe config.json