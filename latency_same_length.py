import serial
import time
import random
import string
import matplotlib.pyplot as plt
from statistics import mean
import re
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
            print(f"[{timestamp}][{ser.port} IN] {response}")
            message_queue.put((ser.port, timestamp, response))

def parse_timestamp(timestamp_str):
    return datetime.strptime(timestamp_str, '%H:%M:%S.%f')

def send_and_receive(sender, receiver, payload, message_queue, timeout=5):
    command = f"hirg -r {receiver['node_id']} -p {payload}\n"
    timestamp = current_milli_time()
    print(f"[{timestamp}][{sender['port']} OUT] {command.strip()}")
    start_time = datetime.now()
    sender['serial'].write(command.encode())

    response_found = False
    expected_message = f"Decrypted message: {payload}"
    prefix_pattern = r"Decrypted message: "

    events = []
    
    while (datetime.now() - start_time) < timedelta(seconds=timeout):
        try:
            port, timestamp, message = message_queue.get(timeout=0.1)
            events.append((port, timestamp, message))
            
            if port == receiver['port'] and message.startswith(prefix_pattern):
                if message == expected_message:
                    response_found = True
                    break

        except queue.Empty:
            continue

    if response_found:
        encryption_time = None
        decryption_time = None
        outbound_time = None
        inbound_time = None

        for port, timestamp, message in events:
            if port == sender['port']:
                if "Encryption time /us:" in message:
                    encryption_time = int(message.split(":")[1].strip()) / 1000  # Convert to ms
                elif "OUTBOUND" in message:
                    outbound_time = parse_timestamp(timestamp)
            elif port == receiver['port']:
                if "INBOUND" in message:
                    inbound_time = parse_timestamp(timestamp)
                elif "Decryption time /us:" in message:
                    decryption_time = int(message.split(":")[1].strip()) / 1000  # Convert to ms

        if encryption_time is not None and decryption_time is not None and outbound_time is not None and inbound_time is not None:
            air_time = (inbound_time - outbound_time).total_seconds() * 1000  # Convert to milliseconds
            
            if air_time < 0:
                print(f"Warning: Negative air time detected ({air_time:.2f} ms). Setting to 0 ms.")
                air_time = 0

            total_latency = encryption_time + air_time + decryption_time

            return {
                'total_latency': total_latency,
                'encryption_time': encryption_time,
                'air_time': air_time,
                'decryption_time': decryption_time
            }, True

    timestamp = current_milli_time()
    print(f"[{timestamp}] Timeout reached or incomplete data")
    return None, False

def main():
    # Configuration
    nodes = [
        {'port': 'COM13', 'node_id': 480652657},
        {'port': 'COM5', 'node_id': 2385360021}
    ]
    payload_length = 20
    num_iterations = 1000
    timeout = 5  # Timeout in seconds

    # Open serial ports
    for node in nodes:
        node['serial'] = serial.Serial(node['port'], 115200, timeout=1)

    timestamp = current_milli_time()
    print(f"[{timestamp}] Serial ports opened:")
    for node in nodes:
        print(f"[{timestamp}] Port: {node['port']}, Node ID: {node['node_id']}")

    message_queue = queue.Queue()
    stop_event = threading.Event()

    # Start reading threads
    threads = []
    for node in nodes:
        thread = threading.Thread(target=read_from_port, args=(node['serial'], message_queue, stop_event))
        thread.start()
        threads.append(thread)

    latencies = []
    encryption_times = []
    air_times = []
    decryption_times = []
    packet_loss = 0

    try:
        for i in range(num_iterations):
            timestamp = current_milli_time()
            print(f"\n[{timestamp}] --- Iteration {i+1} ---")
            payload = generate_payload(payload_length)
            print(f"[{timestamp}] Generated payload: {payload}")
            result, integrity = send_and_receive(nodes[0], nodes[1], payload, message_queue, timeout)
            
            if integrity:
                latencies.append(result['total_latency'])
                encryption_times.append(result['encryption_time'])
                air_times.append(result['air_time'])
                decryption_times.append(result['decryption_time'])
                print(f"[{timestamp}] Total Latency: {result['total_latency']:.2f} ms, "
                      f"Encryption: {result['encryption_time']:.2f} ms, "
                      f"Air Time: {result['air_time']:.2f} ms, "
                      f"Decryption: {result['decryption_time']:.2f} ms")
            else:
                packet_loss += 1
                print(f"[{timestamp}] Packet loss or timeout")

    finally:
        stop_event.set()
        for thread in threads:
            thread.join()

        for node in nodes:
            node['serial'].close()
        timestamp = current_milli_time()
        print(f"\n[{timestamp}] Serial ports closed")

    # Calculate statistics
    avg_total_latency = mean(latencies) if latencies else 0
    avg_encryption_time = mean(encryption_times) if encryption_times else 0
    avg_air_time = mean(air_times) if air_times else 0
    avg_decryption_time = mean(decryption_times) if decryption_times else 0
    packet_loss_rate = (packet_loss / num_iterations) * 100

    # Plot latency components
    plt.figure(figsize=(12, 6))
    iterations = range(1, len(latencies) + 1)
    plt.bar(iterations, encryption_times, label='Encryption', color='r', alpha=0.7)
    plt.bar(iterations, air_times, bottom=encryption_times, label='Transmission time', color='g', alpha=0.7)
    plt.bar(iterations, decryption_times, bottom=[sum(x) for x in zip(encryption_times, air_times)], label='Decryption', color='b', alpha=0.7)
    
    plt.title('Latency Components, message length = ' + str(payload_length))
    plt.xlabel('Iteration')
    plt.ylabel('Time (ms)')
    plt.legend()
    plt.grid(True)

    # Add average latency line
    plt.axhline(y=avg_total_latency, color='k', linestyle='--', label=f'Avg Total: {avg_total_latency:.2f} ms')

    # Create summary text
    summary = f"Summary:\n"
    summary += f"Average Total Latency: {avg_total_latency:.2f} ms\n"
    summary += f"Average Encryption Time: {avg_encryption_time:.2f} ms\n"
    summary += f"Average Transmission time: {avg_air_time:.2f} ms\n"
    summary += f"Average Decryption Time: {avg_decryption_time:.2f} ms\n"
    summary += f"Packet Loss Rate: {packet_loss_rate:.2f}%\n"
    summary += f"Total Iterations: {num_iterations}\n"
    summary += f"Successful Transmissions: {len(latencies)}\n"
    summary += f"Lost Packets: {packet_loss}"

    # Add summary text to the plot
    plt.annotate(summary, xy=(-0.1, 1.1), xycoords='axes fraction',
                 verticalalignment='top', horizontalalignment='left',
                 bbox=dict(boxstyle='round', facecolor='white', alpha=0.8))

    # Save the plot as a high-resolution PNG file
    filename = datetime.now().strftime('%Y%m%d_%H%M%S')
    plt.savefig(f'{filename}_latency_components.png', dpi=300, bbox_inches='tight')

    # Show the plot
    plt.show()

    timestamp = current_milli_time()
    print(f"[{timestamp}] Plot saved as '{filename}_latency_components.png'")
    print(f"[{timestamp}] {summary}")

if __name__ == "__main__":
    main()