import socket
import sys

PORT = 5555
HOST = "127.0.0.1"

def run_tcp_server(host=HOST, port=PORT):
    with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as server_socket:
        server_socket.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        
        server_socket.bind((host, port))
        
        server_socket.listen(1)
        print(f"Server {host}:{port}")
        print("Waiting...")
        
        while True:
            client_socket, client_addr = server_socket.accept()
            print(f"Connected: {client_addr}")
            
            with client_socket:
                while True:
                    data = client_socket.recv(1024)
                    
                    if not data:
                        print(f"Connection {client_addr} closed")
                        break
                    
                    print(f"received: {data.decode('utf-8', errors='replace')}")
                                        
                    if data.strip().lower() == b'exit':
                        print("Exit cmd received")
                        return

if __name__ == "__main__":
    try:
        run_tcp_server()
    except KeyboardInterrupt:
        print("Stoped by user")
    except Exception as e:
        print(f"Error: {e}")
        sys.exit(1)