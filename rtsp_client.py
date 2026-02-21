import socket

with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as s:
    s.connect(("localhost", 5801))
    s.sendall(b"OPTIONS rtsp://localhost:5801/lifecam RTSP/1.0\r\nCSeq: 1\r\n\r\n")
    data = s.recv(1024)
    print("Received", repr(data))
