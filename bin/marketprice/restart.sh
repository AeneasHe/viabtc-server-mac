### 
# @Description: 
 # @Author: Aeneas
 # @Github: https://github.com/cofepy
 # @Date: 2019-10-27 19:31:10
 # @LastEditors: Aeneas
 # @LastEditTime: 2019-10-27 19:32:57
 ###
#!/bin/bash

rm ./*.log
killall -9 marketprice
sleep 1
./marketprice config.json