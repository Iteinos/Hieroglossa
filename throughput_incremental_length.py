import serial
import time
import json
import random
import string
import matplotlib.pyplot as plt
from statistics import mean
import threading
import queue
from datetime import datetime, timedelta
import numpy as np

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

def run_throughput_test(sender, receiver, payload, frequency, iterations, message_queue, encrypted=False, timeout=10):
    command = f"hirg -r {receiver['node_id']} -{('s' if encrypted else 'p')} {payload} -t {frequency} -i {iterations}\n"
    timestamp = current_milli_time()
    print(f"[{timestamp}][{sender['port']} OUT] {command.strip()}")
    sender['serial'].write(command.encode())

    received_messages = 0
    start_time = datetime.now()
    expected_duration = iterations / frequency
    end_time = start_time + timedelta(seconds=max(expected_duration * 2, timeout))

    received_message_numbers = set()

    while datetime.now() < end_time and len(received_message_numbers) < iterations:
        try:
            port, timestamp, message = message_queue.get(timeout=0.1)
            if port == receiver['port']:
                if "Received from" in message and "msg=" in message:
                    msg_content = message.split("msg=", 1)[1].strip()
                    if msg_content.lower() == 'null':
                        print(f"Received null message: {message}")
                        continue
                    try:
                        message_data = json.loads(msg_content)
                        if isinstance(message_data, dict):
                            message_number = message_data.get('message_number')
                            if message_number and message_number not in received_message_numbers:
                                received_message_numbers.add(message_number)
                                received_messages += 1
                        else:
                            print(f"Unexpected message format: {message_data}")
                    except json.JSONDecodeError:
                        print(f"Error decoding JSON from message: {message}")
        except queue.Empty:
            continue

    actual_end_time = datetime.now()
    duration = (actual_end_time - start_time).total_seconds()
    throughput = received_messages / duration if duration > 0 else 0

    return {
        'sent': iterations,
        'received': received_messages,
        'duration': duration,
        'throughput': throughput,
        'packet_loss': (iterations - received_messages) / iterations * 100
    }
import numpy as np

def plot_results(all_results, payload_length):
    plt.figure(figsize=(12, 8))
    
    frequencies = sorted(all_results.keys())
    throughput = [r['throughput'] for r in all_results.values()]

    # Function to add statistics
    def add_stats(data, color):
        stats = {
            'max': max(data),
            'min': min(data),
            'avg': np.mean(data),
            'median': np.median(data)
        }

        for stat, value in stats.items():
            plt.axhline(y=value, color=color, linestyle='--', alpha=0.5)
            plt.text(frequencies[-1] * 1.02, value, f'{stat.capitalize()}: {value:.2f}', 
                     color=color, verticalalignment='center')

        return stats

    # Plot throughput
    plt.plot(frequencies, throughput, 'bo-', label='Throughput')
    plt.xlabel('Frequency (messages/second)')
    plt.ylabel('Throughput (messages/second)')
    plt.title(f'Throughput vs Frequency (Message Length: {payload_length} bytes)')
    plt.grid(True)

    # Add throughput statistics
    throughput_stats = add_stats(throughput, 'blue')

    plt.tight_layout()
    filename = datetime.now().strftime('%Y%m%d_%H%M%S')
    plt.savefig(f'{filename}_throughput_test.png', dpi=300, bbox_inches='tight')
    plt.show()

    # Print summary
    print("\nSummary:")
    for freq, result in all_results.items():
        print(f"Frequency: {freq} msg/s")
        print(f"  Sent: {result['sent']}")
        print(f"  Received: {result['received']}")
        print(f"  Packet Loss Rate: {(result['sent'] - result['received']) / result['sent'] * 100:.2f}%")
        print(f"  Duration: {result['duration']:.2f} s")
        print(f"  Throughput: {result['throughput']:.2f} msg/s")
        print()

    print("Throughput Statistics:")
    for stat, value in throughput_stats.items():
        print(f"  {stat.capitalize()}: {value:.2f} msg/s")

    return throughput_stats

def plot_heatmap_results(all_results):
    plt.figure(figsize=(20, 12))  # Increased width to accommodate more frequency labels
    
    frequencies = sorted(list(all_results.keys()))
    payload_lengths = sorted(list(all_results[frequencies[0]].keys()))
    
    data = np.zeros((len(frequencies), len(payload_lengths)))
    
    for i, freq in enumerate(frequencies):
        for j, length in enumerate(payload_lengths):
            data[i, j] = all_results[freq][length]['throughput']
    
    # Create the heatmap
    im = plt.imshow(data, cmap='viridis', aspect='auto', origin='lower')
    plt.colorbar(im, label='Throughput (messages/second)')
    
    # Add text annotations
    for i in range(len(frequencies)):
        for j in range(len(payload_lengths)):
            value = data[i, j]
            color = 'white' if value < np.mean(data) else 'black'
            plt.text(j, i, f'{value:.2f}', ha='center', va='center', color=color, fontsize=8)
    
    plt.xlabel('Message Length (bytes)')
    plt.ylabel('Frequency (messages/second)')
    plt.title('Mesh Network Throughput')
    plt.xticks(range(len(payload_lengths)), payload_lengths)
    plt.yticks(range(0, len(frequencies), 10), frequencies[::10])  # Show every 10th frequency label
    
    filename = datetime.now().strftime('%Y%m%d_%H%M%S')
    plt.savefig(f'{filename}_throughput_heatmap.png', dpi=300, bbox_inches='tight')
    plt.show()

    # Print summary
    print("\nSummary:")
    for freq in frequencies:
        for length in payload_lengths:
            result = all_results[freq][length]
            print(f"Frequency: {freq} msg/s, Length: {length} bytes")
            print(f"  Sent: {result['sent']}")
            print(f"  Received: {result['received']}")
            print(f"  Packet Loss Rate: {(result['sent'] - result['received']) / result['sent'] * 100:.2f}%")
            print(f"  Duration: {result['duration']:.2f} s")
            print(f"  Throughput: {result['throughput']:.2f} msg/s")
            print()
            
def main():
    # Configuration
    nodes = [
        {'port': 'COM13', 'node_id': 853210837},
        {'port': 'COM5', 'node_id': 480652657} 
    ]
    min_payload_length = 10
    max_payload_length = 100
    payload_length_step = 5
    min_frequency = 1
    max_frequency = 100
    frequency_step = 5
    iterations = 10
    encrypted = False
    delay_between_tests = 1  # Add a small delay (in seconds) between tests if needed

    # Open serial ports
    for node in nodes:
        node['serial'] = serial.Serial(node['port'], 115200, timeout=1)

    message_queue = queue.Queue()
    stop_event = threading.Event()

    # Start reading threads
    threads = []
    for node in nodes:
        thread = threading.Thread(target=read_from_port, args=(node['serial'], message_queue, stop_event))
        thread.start()
        threads.append(thread)

    all_results = {}

    try:
        for frequency in range(min_frequency, max_frequency + 1, frequency_step):
            all_results[frequency] = {}
            for payload_length in range(min_payload_length, max_payload_length + 1, payload_length_step):
                print(f"\n--- Testing {'encrypted' if encrypted else 'unencrypted'} throughput at {frequency} msg/s with payload length {payload_length} bytes ---")
                payload = generate_payload(payload_length)
                
                # Clear the message queue before each test
                while not message_queue.empty():
                    message_queue.get_nowait()
                
                try:
                    result = run_throughput_test(nodes[0], nodes[1], payload, frequency, iterations, message_queue, encrypted)
                    all_results[frequency][payload_length] = result
                    print(f"Sent: {result['sent']} messages")
                    print(f"Received: {result['received']} messages")
                    print(f"Throughput: {result['throughput']:.2f} msg/s")
                    print(f"Packet Loss: {result['packet_loss']:.2f}%")
                    print(f"Duration: {result['duration']:.2f} s")
                except Exception as e:
                    print(f"Error during test: {e}")
                    print(f"Skipping test for frequency {frequency} and payload length {payload_length}")
                
                # Add a small delay between tests
                time.sleep(1)

    except KeyboardInterrupt:
        print("\nTest interrupted by user.")
    finally:
        stop_event.set()
        for thread in threads:
            thread.join()

        for node in nodes:
            node['serial'].close()
        print("\nSerial ports closed")
    plot_heatmap_results(all_results)

if __name__ == "__main__":
    main()