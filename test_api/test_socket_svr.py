import socket  # 导入 socket 模块

# AF_INET,AF_UNIX
s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)  # 创建 socket 对象
# host = socket.gethostname()  # 获取本地主机名
host = "0.0.0.0"
port = 59064  # 设置端口
print(host)

# s.bind(b"/tmp/test.socket")  # 绑定端口 #(host, port)
s.bind((host, port))  # 绑定端口 #(host, port)

s.listen(5)  # 等待客户端连接
while True:
    c, addr = s.accept()  # 建立客户端连接
    print("连接地址：", addr)
    c.send("欢迎访问菜鸟教程！".encode("utf-8"))
    c.close()  # 关闭连接

