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

def run_throughput_test(sender, receiver, payload, frequency, iterations, message_queue, encrypted=False, timeout=60):
    command = f"hirg -r {receiver['node_id']} -{('s' if encrypted else 'p')} {payload} -t {frequency} -i {iterations}\n"
    timestamp = current_milli_time()
    print(f"[{timestamp}][{sender['port']} OUT] {command.strip()}")
    sender['serial'].write(command.encode())

    received_messages = 0
    start_time = datetime.now()
    expected_duration = iterations / frequency

    while (datetime.now() - start_time).total_seconds() < max(expected_duration, timeout):
        try:
            port, timestamp, message = message_queue.get(timeout=0.1)
            if port == receiver['port'] and "Received unencrypted message:" in message:
                received_messages += 1
                if received_messages == iterations:
                    break
        except queue.Empty:
            continue

    end_time = datetime.now()
    duration = (end_time - start_time).total_seconds()
    throughput = received_messages / duration

    return {
        'sent': iterations,
        'received': received_messages,
        'duration': duration,
        'throughput': throughput
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

def main():
    # Configuration
    nodes = [
        {'port': 'COM13', 'node_id': 480652657},
        {'port': 'COM5', 'node_id': 2385360021}
    ]
    payload_length = 100
    min_frequency = 1
    max_frequency = 100
    frequency_step = 1
    iterations = 10
    encrypted = False

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
            print(f"\n--- Testing {'encrypted' if encrypted else 'unencrypted'} throughput at {frequency} msg/s ---")
            payload = generate_payload(payload_length)
            result = run_throughput_test(nodes[0], nodes[1], payload, frequency, iterations, message_queue, encrypted)
            all_results[frequency] = result
            print(f"Throughput: {result['throughput']:.2f} msg/s")

    finally:
        stop_event.set()
        for thread in threads:
            thread.join()

        for node in nodes:
            node['serial'].close()
        print("\nSerial ports closed")

    # Plotting
    plot_results(all_results, payload_length)

if __name__ == "__main__":
    main()