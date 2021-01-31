#!/usr/bin/python
# -*- coding: UTF-8 -*-
# 文件名：client.py

import socket  # 导入 socket 模块

s = socket.socket()  # 创建 socket 对象 socket.AF_INET, socket.SOCK_STREAM

# host = socket.gethostname()  # 获取本地主机名
# port = 12345  # 设置端口号

host = "0.0.0.0"
port = 59604  # 设置端口

s.connect((host, port))  #

# s.connect(b"/tmp/test.socket")  # (host, port)
s.send("test".encode("utf-8"))
print(s.recv(1024).decode("utf-8"))
s.close()
