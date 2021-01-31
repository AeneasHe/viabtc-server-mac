#!/usr/bin/python
# -*- coding: UTF-8 -*-

import socket

# 建立一个服务端
server = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
server.bind(("localhost", 6999))  # 绑定要监听的端口
server.listen(5)  # 开始监听 表示可以使用五个链接排队
while True:  # conn就是客户端链接过来而在服务端为期生成的一个链接实例
    conn, addr = server.accept()  # 等待链接,多个链接的时候就会出现问题,其实返回了两个值
    print(conn, addr)
    while True:
        try:
            data = conn.recv(1024)  # 接收数据
            print("recive:", data.decode())  # 打印接收到的数据
            conn.send(data.upper())  # 然后再发送数据
        except ConnectionResetError as e:
            print("关闭了正在占线的链接！")
            break
    conn.close()
