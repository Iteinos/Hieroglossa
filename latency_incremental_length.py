import serial
import time
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

def parse_timestamp(timestamp_str):
    return datetime.strptime(timestamp_str, '%H:%M:%S.%f')

def send_and_receive(sender, receiver, payload, message_queue, encrypted=True, timeout=10):
    command = f"hirg -r {receiver['node_id']} -{('s' if encrypted else 'p')} {payload}\n"
    timestamp = current_milli_time()
    print(f"[{timestamp}][{sender['port']} OUT] {command.strip()}")
    sender['serial'].write(command.encode())

    response_found = False
    expected_message = f"Decrypted message from {sender['node_id']}: {payload}"
    prefix_pattern = f"Decrypted message from {sender['node_id']}:"

    events = []
    
    start_time = datetime.now()
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
        encryption_time = 0 if not encrypted else None
        decryption_time = 0 if not encrypted else None
        outbound_time = None
        inbound_time = None

        for port, timestamp, message in events:
            if port == sender['port']:
                if encrypted and "Encryption time /us:" in message:
                    encryption_time = int(message.split(":")[1].strip()) / 1000  # Convert to ms
                elif "OUTBOUND" in message:
                    outbound_time = parse_timestamp(timestamp)
            elif port == receiver['port']:
                if "INBOUND" in message:
                    inbound_time = parse_timestamp(timestamp)
                elif encrypted and "Decryption time /us:" in message:
                    decryption_time = int(message.split(":")[1].strip()) / 1000  # Convert to ms

        if outbound_time is not None and inbound_time is not None:
            air_time = (inbound_time - outbound_time).total_seconds() * 1000  # Convert to milliseconds
            
            if air_time < 0:
                print(f"Warning: Negative air time detected ({air_time:.2f} ms). Setting to 0 ms.")
                air_time = 0

            total_latency = (encryption_time or 0) + air_time + (decryption_time or 0)

            return {
                'total_latency': total_latency,
                'encryption_time': encryption_time or 0,
                'air_time': air_time,
                'decryption_time': decryption_time or 0
            }, True

    timestamp = current_milli_time()
    print(f"[{timestamp}] Timeout reached or incomplete data")
    return None, False

def run_tests(sender, receiver, message_queue, payload_length, num_iterations, encrypted=True, timeout=10):
    results = []
    for _ in range(num_iterations):
        payload = generate_payload(payload_length)
        result, success = send_and_receive(sender, receiver, payload, message_queue, encrypted, timeout)
        if success:
            results.append(result)
    return results

def plot_results(all_results, avg_results):
    # Create the main plot
    plt.figure(figsize=(20, 10))
    
    lengths = sorted(all_results.keys())
    bar_width = 0.8
    colors = ['r', 'g', 'b']
    components = ['encryption_time', 'air_time', 'decryption_time']
    labels = ['Encryption', 'Transmission', 'Decryption']

    # Calculate the total number of bars and their positions
    total_bars = sum(len(all_results[length]) for length in lengths)
    bar_positions = np.arange(total_bars)

    # Create mapping from bar position to message length
    length_mapping = []
    for length in lengths:
        length_mapping.extend([length] * len(all_results[length]))

    # Plot individual bars
    bottom = np.zeros(total_bars)
    for i, component in enumerate(components):
        heights = [result[component] for length in lengths for result in all_results[length]]
        plt.bar(bar_positions, heights, bar_width, bottom=bottom, color=colors[i], alpha=0.6, label=labels[i])
        bottom += heights

    # Plot average lines
    avg_positions = []
    for length in lengths:
        start = len(avg_positions)
        avg_positions.extend(np.arange(start, start + len(all_results[length])))

    for i, component in enumerate(components):
        avg_values = [avg_results[length][component] for length in lengths for _ in all_results[length]]
        plt.plot(avg_positions, avg_values, color=colors[i], linewidth=2, label=f'Avg {labels[i]}')

    # Plot total latency average line
    total_avg_values = [avg_results[length]['total_latency'] for length in lengths for _ in all_results[length]]
    plt.plot(avg_positions, total_avg_values, color='k', linewidth=2, label='Avg Total Latency')

    plt.xlabel('Experiment Number')
    plt.ylabel('Time (ms)')
    plt.title('Latency Components for All Experiments')
    plt.legend(loc='upper left', bbox_to_anchor=(1, 1))
    plt.grid(True)

    # Create custom x-ticks to show message lengths
    unique_positions = [np.mean(np.where(np.array(length_mapping) == length)[0]) for length in lengths]
    plt.xticks(unique_positions, lengths)
    plt.xlabel('Message Length')

    # Add a second x-axis for experiment numbers
    ax2 = plt.twiny()
    ax2.set_xlim(plt.gca().get_xlim())
    ax2.set_xticks(bar_positions)
    ax2.set_xticklabels(range(1, total_bars + 1))
    ax2.set_xlabel('Experiment Number')

    # Save the main plot
    filename = datetime.now().strftime('%Y%m%d_%H%M%S')
    plt.savefig(f'{filename}_latency_components.png', dpi=300, bbox_inches='tight')
    plt.tight_layout()
    plt.show()

    # Create a new figure for the summary
    plt.figure(figsize=(10, len(lengths) * 0.5))
    summary = "Summary:\n"
    for length in lengths:
        summary += f"Length {length}:\n"
        summary += f"  Avg Total: {avg_results[length]['total_latency']:.2f} ms\n"
        summary += f"  Avg Encryption: {avg_results[length]['encryption_time']:.2f} ms\n"
        summary += f"  Avg Transmission: {avg_results[length]['air_time']:.2f} ms\n"
        summary += f"  Avg Decryption: {avg_results[length]['decryption_time']:.2f} ms\n"
        summary += f"  Iterations: {len(all_results[length])}\n\n"

    plt.text(0.05, 0.95, summary, verticalalignment='top', horizontalalignment='left', 
             transform=plt.gca().transAxes, fontsize=10, family='monospace')
    plt.axis('off')
    plt.tight_layout()
    
    # Save the summary plot
    plt.savefig(f'{filename}_summary.png', dpi=300, bbox_inches='tight')
    plt.show()

    timestamp = current_milli_time()
    print(f"[{timestamp}] Plots saved as '{filename}_latency_components.png' and '{filename}_summary.png'")
    print(summary)


def main():
    nodes = [
        {'port': 'COM13', 'node_id': 480652657},
        {'port': 'COM5', 'node_id': 2385360021}
    ]
    min_length = 5
    max_length = 100
    length_increment = 1
    tests_per_length = 20
    encrypted = True  # Set this to True for encrypted tests, False for unencrypted
    timeout = 10  # Increased timeout

    # Open serial ports
    for node in nodes:
        try:
            node['serial'] = serial.Serial(node['port'], 115200, timeout=1)
        except serial.SerialException as e:
            print(f"Error opening serial port {node['port']}: {e}")
            return

    message_queue = queue.Queue()
    stop_event = threading.Event()

    # Start reading threads
    threads = []
    for node in nodes:
        thread = threading.Thread(target=read_from_port, args=(node['serial'], message_queue, stop_event))
        thread.start()
        threads.append(thread)

    all_results = {}
    avg_results = {}

    try:
        for length in range(min_length, max_length + 1, length_increment):
            print(f"\n--- Testing {'encrypted' if encrypted else 'unencrypted'} message length: {length} ---")
            results = run_tests(nodes[0], nodes[1], message_queue, length, tests_per_length, encrypted, timeout)
            
            if results:
                all_results[length] = results
                avg_results[length] = {
                    'encryption_time': mean([r['encryption_time'] for r in results]),
                    'air_time': mean([r['air_time'] for r in results]),
                    'decryption_time': mean([r['decryption_time'] for r in results]),
                    'total_latency': mean([r['total_latency'] for r in results])
                }
                print(f"Average latency for length {length}: {avg_results[length]['total_latency']:.2f} ms")
            else:
                print(f"No valid results for length {length}, skipping...")

    except Exception as e:
        print(f"An error occurred during testing: {e}")

    finally:
        stop_event.set()
        for thread in threads:
            thread.join()

        for node in nodes:
            try:
                node['serial'].close()
            except:
                pass
        print("\nSerial ports closed")

    # Plotting
    if all_results and avg_results:
        try:
            plot_results(all_results, avg_results)
        except Exception as e:
            print(f"Error during plotting: {e}")
    else:
        print("No results to plot.")

if __name__ == "__main__":
    main()