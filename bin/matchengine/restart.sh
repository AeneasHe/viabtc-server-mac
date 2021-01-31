### 
# @Description: 
 # @Author: Aeneas
 # @Github: https://github.com/cofepy
 # @Date: 2019-10-27 19:31:10
 # @LastEditors: Aeneas
 # @LastEditTime: 2019-10-27 19:32:44
 ###
#!/bin/bash

rm ./*.log
killall -9 matchengine
sleep 3
./matchengine config.json