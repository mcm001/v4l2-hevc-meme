import socket

s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
s.bind(("0.0.0.0", 18888))

while True:
    msg = s.recvmsg(4096)
    print(msg)
