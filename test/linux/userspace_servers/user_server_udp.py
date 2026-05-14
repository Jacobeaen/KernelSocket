import socket
import sys

PORT = 6666
HOST = "127.0.0.1"

def run_udp_server(host=HOST, port=PORT):
    with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as server_socket:
        server_socket.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        
        server_socket.bind((host, port))
        
        print(f"UDP Server {host}:{port}")
        print("Waiting for datagrams...")
        
        while True:
            data, client_addr = server_socket.recvfrom(1024)
            
            print(f"Received from {client_addr}: {data.decode('utf-8', errors='replace')}")
            
            if data.strip().lower() == b'exit':
                print("Received exit command, shutting down")
                break
            
            server_socket.sendto(b"ACK: Message received", client_addr)

if __name__ == "__main__":
    try:
        run_udp_server()
    except KeyboardInterrupt:
        print("\nStopped by user")
    except Exception as e:
        print(f"Error: {e}")
        sys.exit(1)