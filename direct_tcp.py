import socket
import json
import time

def listen_for_painlessmesh_tcp(node_id, port=5555, timeout=300):
    server_socket = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    server_socket.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    server_socket.bind(('', port))
    server_socket.listen(5)  # Listen for incoming connections

    print(f"Listening for PainlessMesh TCP connections on port {port}")
    print(f"Looking for node ID: {node_id}")
    print(f"Will listen for {timeout} seconds. Press Ctrl+C to stop earlier.")

    start_time = time.time()

    try:
        while time.time() - start_time < timeout:
            try:
                client_socket, addr = server_socket.accept()
                print(f"Accepted connection from {addr}")
                data = client_socket.recv(1024)
                
                print(f"Received message from {addr}:")
                
                try:
                    json_data = json.loads(data.decode('utf-8'))
                    print(f"Decoded JSON: {json.dumps(json_data, indent=2)}")
                    
                    # Check if the node ID is in the JSON data
                    if str(node_id) in str(json_data):
                        print(f"Found node ID {node_id} in message!")
                except json.JSONDecodeError:
                    print("Unable to decode as JSON. Raw data:")
                    print(data.hex())
                
                print("-" * 40)
                client_socket.close()

            except socket.timeout:
                # This is expected, just continue
                pass
            except Exception as e:
                print(f"Error receiving data: {e}")

    except KeyboardInterrupt:
        print("Listening stopped by user.")
    finally:
        server_socket.close()

# The node ID you're looking for
node_id = "1ca6272d"

# PainlessMesh default port
painlessmesh_port = 5555

listen_for_painlessmesh_tcp(node_id, painlessmesh_port)
