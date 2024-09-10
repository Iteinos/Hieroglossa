import serial
import time
import random
import string
import matplotlib.pyplot as plt
import threading
import queue
from datetime import datetime, timedelta

def current_milli_time():
    return datetime.now().strftime('%H:%M:%S.%f')[:-3]

def generate_payload(length):
    return ''.join(random.choices(string.ascii_letters + string.digits, k=length))

def read_from_port(ser, message_queue, stop_event):
    while not stop_event.is_set():
        if ser.in_waiting:
            response = ser.readline().decode().strip()
            timestamp = current_milli_time()
            if "msg=" in response:
                print(f"[{timestamp}][{ser.port} IN] {response}")
                message_queue.put((ser.port, timestamp, response))
            elif ser.port in response:  # This will catch the outgoing messages
                print(f"[{timestamp}][{ser.port} OUT] {response}")

def send_packet(sender, receiver, payload):
    command = f"hirg -r {receiver['node_id']} -p {payload}\n"
    timestamp = current_milli_time()
    print(f"[{timestamp}][{sender['port']} OUT] {command.strip()}")
    sender['serial'].write(command.encode())

def packet_loss_test(sender, receiver, message_queue, results, stop_event):
    payload_length = 20
    packets_sent = 0
    packets_received = 0
    sent_payloads = []
    send_interval = 1.0  # 1 second between sends

    while not stop_event.is_set():
        payload = generate_payload(payload_length)
        send_packet(sender, receiver, payload)
        sent_payloads.append(payload)
        packets_sent += 1
        print(f"[{current_milli_time()}] Packet sent: {payload}")

        start_time = time.time()
        while time.time() - start_time < send_interval:
            try:
                port, timestamp, message = message_queue.get(timeout=0.1)
                if port == receiver['port'] and "msg=" in message:
                    received_payload = message.split("msg=")[1].strip()
                    if received_payload in sent_payloads:
                        packets_received += 1
                        sent_payloads.remove(received_payload)
                        print(f"[{timestamp}] Packet received successfully: {received_payload}")
                    else:
                        print(f"[{timestamp}] Received unexpected payload: {received_payload}")
            except queue.Empty:
                continue

        # Check for lost packets
        while len(sent_payloads) > 5:  # Allow for some delay, but not too much
            lost_payload = sent_payloads.pop(0)
            print(f"[{current_milli_time()}] Packet lost: {lost_payload}")

        loss_rate = (packets_sent - packets_received) / packets_sent * 100
        results.append((packets_sent, loss_rate))

        # Wait for the remainder of the send interval
        time.sleep(max(0, send_interval - (time.time() - start_time)))

def plot_results(results):
    packets_sent, loss_rates = zip(*results)

    plt.figure(figsize=(12, 6))
    plt.plot(packets_sent, loss_rates)
    plt.xlabel('Packets Sent')
    plt.ylabel('Packet Loss Rate (%)')
    plt.title('Packet Loss Rate Over Time')
    plt.grid(True)

    filename = datetime.now().strftime('%Y%m%d_%H%M%S_packet_loss.png')
    plt.savefig(filename, dpi=300, bbox_inches='tight')
    plt.show()

    print(f"Plot saved as '{filename}'")

def main():
    nodes = [
        {'port': 'COM5', 'node_id': 480652657},
        {'port': 'COM13', 'node_id': 853210837}
    ]

    for node in nodes:
        node['serial'] = serial.Serial(node['port'], 115200, timeout=1)

    timestamp = current_milli_time()
    print(f"[{timestamp}] Serial ports opened:")
    for node in nodes:
        print(f"[{timestamp}] Port: {node['port']}, Node ID: {node['node_id']}")

    message_queue = queue.Queue()
    stop_event = threading.Event()

    threads = []
    for node in nodes:
        thread = threading.Thread(target=read_from_port, args=(node['serial'], message_queue, stop_event))
        thread.start()
        threads.append(thread)

    results = []
    test_thread = threading.Thread(target=packet_loss_test, args=(nodes[0], nodes[1], message_queue, results, stop_event))
    test_thread.start()

    print("Press Enter to stop the test and plot results...")
    input()

    stop_event.set()
    for thread in threads:
        thread.join()
    test_thread.join()

    for node in nodes:
        node['serial'].close()
    print("\nSerial ports closed")

    if results:
        plot_results(results)
    else:
        print("No results to plot.")

if __name__ == "__main__":
    main()