import serial
import time
import random
import string
import matplotlib.pyplot as plt
import threading
import queue
from datetime import datetime, timedelta
import numpy as np
from statistics import mean

def current_milli_time():
    return datetime.now().strftime('%H:%M:%S.%f')[:-3]

def read_from_port(ser, message_queue, stop_event):
    while not stop_event.is_set():
        if ser.in_waiting:
            response = ser.readline().decode().strip()
            timestamp = current_milli_time()
            print(f"[{timestamp}][{ser.port} IN] {response}")
            message_queue.put((ser.port, timestamp, response))

def send_and_receive(ser, node_id, length, message_queue, timeout=10):
    command = f"hirg -r {node_id} -l {length}\n"
    timestamp = current_milli_time()
    print(f"[{timestamp}][{ser.port} OUT] {command.strip()}")
    ser.write(command.encode())

    response_found = False
    events = []

    start_time = datetime.now()
    while (datetime.now() - start_time) < timedelta(seconds=timeout):
        try:
            port, timestamp, message = message_queue.get(timeout=0.1)
            events.append((port, timestamp, message))

            if "Round-trip latency /us:" in message:
                latency_str = message.split("Round-trip latency /us:")[1].strip()
                latency = int(latency_str.split()[0]) / 2000  # Convert to one-way latency in ms
                response_found = True
                break

        except queue.Empty:
            continue

    if response_found:
        return {
            'total_latency': latency,
        }, True

    timestamp = current_milli_time()
    print(f"[{timestamp}] Timeout reached or incomplete data")
    return None, False

import time
def run_tests(ser, node_id, message_queue, payload_length, num_iterations, timeout=10):
    results = []
    for _ in range(num_iterations):
        result, success = send_and_receive(ser, node_id, payload_length, message_queue, timeout)
        if success:
            results.append(result)
        time.sleep(0.5)  # Add delay between tests
    return results

import numpy as np
import matplotlib.pyplot as plt
from statistics import mean, median

def plot_results(all_results, avg_results):
    plt.figure(figsize=(20, 10))

    lengths = sorted(all_results.keys())
    bar_width = 0.8
    colors = ['b', 'g', 'r', 'c', 'm']
    labels = ['Individual Tests', 'Min', 'Median', 'Avg', 'Max']

    total_bars = sum(len(all_results[length]) for length in lengths)
    bar_positions = np.arange(total_bars)

    length_mapping = []
    for length in lengths:
        length_mapping.extend([length] * len(all_results[length]))

    # Plot individual test results as bars
    heights = [result['total_latency'] for length in lengths for result in all_results[length]]
    plt.bar(bar_positions, heights, bar_width, color=colors[0], alpha=0.6, label=labels[0])

    # Calculate and plot min, median, avg, and max lines
    avg_positions = []
    for length in lengths:
        start = len(avg_positions)
        avg_positions.extend(np.arange(start, start + len(all_results[length])))

    for i, stat in enumerate(['min', 'median', 'avg', 'max']):
        if stat == 'min':
            values = [min(result['total_latency'] for result in all_results[length]) for length in lengths for _ in all_results[length]]
        elif stat == 'median':
            values = [median(result['total_latency'] for result in all_results[length]) for length in lengths for _ in all_results[length]]
        elif stat == 'avg':
            values = [avg_results[length]['total_latency'] for length in lengths for _ in all_results[length]]
        else:  # max
            values = [max(result['total_latency'] for result in all_results[length]) for length in lengths for _ in all_results[length]]
        
        plt.plot(avg_positions, values, color=colors[i+1], linewidth=2, label=f'{labels[i+1]} Latency')

    plt.xlabel('Payload Length (bytes)')
    plt.ylabel('Time (ms)')
    plt.title('One-way Latency, Incremental Payload Length')
    plt.legend(loc='upper left', bbox_to_anchor=(1, 1))
    plt.grid(True)

    unique_positions = [np.mean(np.where(np.array(length_mapping) == length)[0]) for length in lengths]
    plt.xticks(unique_positions[::5], lengths[::5])  # Show every 5th label to reduce clutter
    plt.xlabel('Payload Length (bytes)')

    filename = datetime.now().strftime('%Y%m%d_%H%M%S')
    plt.savefig(f'{filename}_latency_statistics.png', dpi=300, bbox_inches='tight')
    plt.tight_layout()
    plt.show()

    # Summary plot
    plt.figure(figsize=(10, len(lengths) * 0.5))
    summary = "Summary:\n"
    for length in lengths:
        latencies = [result['total_latency'] for result in all_results[length]]
        summary += f"Length {length}:\n"
        summary += f"  Min: {min(latencies):.2f} ms\n"
        summary += f"  Median: {median(latencies):.2f} ms\n"
        summary += f"  Avg: {mean(latencies):.2f} ms\n"
        summary += f"  Max: {max(latencies):.2f} ms\n"
        summary += f"  Iterations: {len(latencies)}\n\n"

    plt.text(0.05, 0.95, summary, verticalalignment='top', horizontalalignment='left', 
             transform=plt.gca().transAxes, fontsize=10, family='monospace')
    plt.axis('off')
    plt.tight_layout()

    plt.savefig(f'{filename}_summary.png', dpi=300, bbox_inches='tight')
    plt.show()

    timestamp = current_milli_time()
    print(f"[{timestamp}] Plots saved as '{filename}_latency_statistics.png' and '{filename}_summary.png'")
    print(summary)


def main():
    node = {'port': 'COM13', 'node_id': 480652657}
    min_length = 10
    max_length = 100
    length_increment = 1
    tests_per_length = 10
    timeout = 10

    try:
        node['serial'] = serial.Serial(node['port'], 115200, timeout=1)
    except serial.SerialException as e:
        print(f"Error opening serial port {node['port']}: {e}")
        return

    message_queue = queue.Queue()
    stop_event = threading.Event()

    thread = threading.Thread(target=read_from_port, args=(node['serial'], message_queue, stop_event))
    thread.start()

    all_results = {}
    avg_results = {}

    try:
        for length in range(min_length, max_length + 1, length_increment):
            print(f"\n--- Testing message length: {length} ---")
            results = run_tests(node['serial'], node['node_id'], message_queue, length, tests_per_length, timeout)
            
            if results:
                all_results[length] = results
                latencies = [r['total_latency'] for r in results]
                avg_results[length] = {
                    'total_latency': mean(latencies)
                }
                print(f"Statistics for length {length}:")
                print(f"  Min: {min(latencies):.2f} ms")
                print(f"  Median: {median(latencies):.2f} ms")
                print(f"  Avg: {avg_results[length]['total_latency']:.2f} ms")
                print(f"  Max: {max(latencies):.2f} ms")
            else:
                print(f"No valid results for length {length}, skipping...")
    except Exception as e:
        print(f"An error occurred during testing: {e}")

    finally:
        stop_event.set()
        thread.join()

        try:
            node['serial'].close()
        except:
            pass
        print("\nSerial port closed")

    if all_results and avg_results:
        try:
            plot_results(all_results, avg_results)
        except Exception as e:
            print(f"Error during plotting: {e}")
    else:
        print("No results to plot.")

if __name__ == "__main__":
    main()
