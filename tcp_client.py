import socket

HOST = '127.0.0.1' 
PORT = 6379        

with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as s:
    try:
        s.connect((HOST, PORT))
        print(f"Connected to {HOST}:{PORT}")
        
        while True:
            msg = input("Enter message (or 'quit' to exit): ")
            if msg.lower() == 'quit':
                break

            s.sendall(msg.encode('utf-8'))

            
            data = s.recv(1024)
            if not data:
                print("Server closed connection")
                break

            print("Received:", data.decode('utf-8'))

    except ConnectionRefusedError:
        print(f"Could not connect to {HOST}:{PORT}")
