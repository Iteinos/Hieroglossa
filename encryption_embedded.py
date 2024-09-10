import serial
import time
import random
import string
import matplotlib.pyplot as plt
import threading
import queue
from datetime import datetime, timedelta
import numpy as np
from statistics import mean, median

def current_milli_time():
    return datetime.now().strftime('%H:%M:%S.%f')[:-3]

def read_from_port(ser, message_queue, stop_event):
    while not stop_event.is_set():
        if ser.in_waiting:
            response = ser.readline().decode().strip()
            timestamp = current_milli_time()
            print(f"[{timestamp}][{ser.port} IN] {response}")
            message_queue.put((ser.port, timestamp, response))

def send_and_receive(ser, plaintext, message_queue, timeout=10):
    command = f"hirg -e {plaintext}\n"
    timestamp = current_milli_time()
    print(f"[{timestamp}][{ser.port} OUT] {command.strip()}")
    ser.write(command.encode())

    encryption_time = None
    decryption_time = None
    events = []

    start_time = datetime.now()
    while (datetime.now() - start_time) < timedelta(seconds=timeout):
        try:
            port, timestamp, message = message_queue.get(timeout=0.1)
            events.append((port, timestamp, message))

            if "Encryption time /us:" in message:
                encryption_time = int(message.split("Encryption time /us:")[1].strip()) / 1000  # Convert to ms
            elif "Decryption time /us:" in message:
                decryption_time = int(message.split("Decryption time /us:")[1].strip()) / 1000  # Convert to ms

            if encryption_time is not None and decryption_time is not None:
                return {
                    'encryption_time': encryption_time,
                    'decryption_time': decryption_time,
                }, True

        except queue.Empty:
            continue

    timestamp = current_milli_time()
    print(f"[{timestamp}] Timeout reached or incomplete data")
    return None, False

def run_tests(ser, message_queue, payload_length, num_iterations, timeout=10):
    results = []
    for _ in range(num_iterations):
        plaintext = ''.join(random.choices(string.ascii_letters + string.digits, k=payload_length))
        result, success = send_and_receive(ser, plaintext, message_queue, timeout)
        if success:
            results.append(result)
        time.sleep(0.1)  # Add delay between tests
    return results

def plot_results(all_results, length_increment, tests_per_length):
    lengths = sorted(all_results.keys())
    
    fig, (ax1, ax2) = plt.subplots(1, 2, figsize=(20, 10))
    bar_width = 0.35
    colors = ['lightblue', 'lightgreen']
    
    # Calculate overall maximum y-value
    all_times = [result[f'{op}_time'] for length in lengths for result in all_results[length] for op in ['encryption', 'decryption']]
    y_max = max(all_times) * 1.1  # Add 10% headroom
    
    for idx, (ax, operation) in enumerate([(ax1, 'encryption'), (ax2, 'decryption')]):
        all_times = [result[f'{operation}_time'] for length in lengths for result in all_results[length]]
        positions = np.arange(len(all_times))
        
        # Plot individual results as bars
        ax.bar(positions, all_times, bar_width, color=colors[idx], alpha=1)
        
        # Add vertical lines to separate different length iterations
        for i in range(1, len(lengths)):
            ax.axvline(x=i*tests_per_length - 0.5, color='gray', linestyle='--', alpha=0.5)
        
        # Calculate and plot median and average lines
        medians = [median([r[f'{operation}_time'] for r in all_results[length]]) for length in lengths]
        averages = [mean([r[f'{operation}_time'] for r in all_results[length]]) for length in lengths]
        
        median_positions = [np.mean(positions[i*tests_per_length:(i+1)*tests_per_length]) for i in range(len(lengths))]
        
        median_line, = ax.plot(median_positions, medians, 'ro-', linewidth=2, label='Median')
        avg_line, = ax.plot(median_positions, averages, 'go-', linewidth=2, label='Average')
        
        # Add value labels to the side of the data points
        for x, y_med, y_avg in zip(median_positions, medians, averages):
            ax.annotate(f'{y_med:.2f}', (x, y_med), xytext=(5, 5), textcoords='offset points', 
                        ha='left', va='center', color=median_line.get_color())
            ax.annotate(f'{y_avg:.2f}', (x, y_avg), xytext=(-5, -5), textcoords='offset points', 
                        ha='right', va='center', color=avg_line.get_color())
        
        ax.set_xlabel('Plaintext Length (bytes)')
        ax.set_ylabel('Time (ms)')
        ax.set_title(f'{operation.capitalize()} Test versus Plaintext Length')
        ax.legend()
        
        # Set x-axis ticks to show payload lengths
        ax.set_xticks(median_positions)
        ax.set_xticklabels(lengths)
        
        # Set y-axis limit to the overall maximum
        ax.set_ylim(0, y_max)
        
    plt.tight_layout()
    filename = datetime.now().strftime('%Y%m%d_%H%M%S')
    plt.savefig(f'{filename}_encryption_decryption_times.png', dpi=300, bbox_inches='tight')
    plt.show()

    # Summary statistics
    summary = "Summary:\n"
    for length in lengths:
        encryption_times = [result['encryption_time'] for result in all_results[length]]
        decryption_times = [result['decryption_time'] for result in all_results[length]]
        
        summary += f"Length {length}:\n"
        summary += f"  Encryption:\n"
        summary += f"    Min: {min(encryption_times):.3f} ms\n"
        summary += f"    Median: {median(encryption_times):.3f} ms\n"
        summary += f"    Avg: {mean(encryption_times):.3f} ms\n"
        summary += f"    Max: {max(encryption_times):.3f} ms\n"
        summary += f"  Decryption:\n"
        summary += f"    Min: {min(decryption_times):.3f} ms\n"
        summary += f"    Median: {median(decryption_times):.3f} ms\n"
        summary += f"    Avg: {mean(decryption_times):.3f} ms\n"
        summary += f"    Max: {max(decryption_times):.3f} ms\n"
        summary += f"  Iterations: {len(encryption_times)}\n\n"

    print(summary)
    
    with open(f'{filename}_summary.txt', 'w') as f:
        f.write(summary)

def main():
    port = 'COM5'
    min_length = 10
    max_length = 100
    length_increment = 5
    tests_per_length = 10
    timeout = 5

    try:
        ser = serial.Serial(port, 115200, timeout=1)
    except serial.SerialException as e:
        print(f"Error opening serial port {port}: {e}")
        return

    message_queue = queue.Queue()
    stop_event = threading.Event()

    thread = threading.Thread(target=read_from_port, args=(ser, message_queue, stop_event))
    thread.start()

    all_results = {}

    try:
        for length in range(min_length, max_length + 1, length_increment):
            print(f"\n--- Testing message length: {length} ---")
            results = run_tests(ser, message_queue, length, tests_per_length, timeout)
            
            if results:
                all_results[length] = results
                encryption_times = [r['encryption_time'] for r in results]
                decryption_times = [r['decryption_time'] for r in results]
                print(f"Statistics for length {length}:")
                print(f"  Encryption:")
                print(f"    Min: {min(encryption_times):.3f} ms")
                print(f"    Median: {median(encryption_times):.3f} ms")
                print(f"    Avg: {mean(encryption_times):.3f} ms")
                print(f"    Max: {max(encryption_times):.3f} ms")
                print(f"  Decryption:")
                print(f"    Min: {min(decryption_times):.3f} ms")
                print(f"    Median: {median(decryption_times):.3f} ms")
                print(f"    Avg: {mean(decryption_times):.3f} ms")
                print(f"    Max: {max(decryption_times):.3f} ms")
            else:
                print(f"No valid results for length {length}, skipping...")
    except Exception as e:
        print(f"An error occurred during testing: {e}")

    finally:
        stop_event.set()
        thread.join()

        try:
            ser.close()
        except:
            pass
        print("\nSerial port closed")

    if all_results:
        try:
            plot_results(all_results, length_increment, tests_per_length)
        except Exception as e:
            print(f"Error during plotting: {e}")
    else:
        print("No results to plot.")

if __name__ == "__main__":
    main()
