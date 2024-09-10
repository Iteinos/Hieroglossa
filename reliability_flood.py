import serial
import time
import threading
import queue
from datetime import datetime
import matplotlib.pyplot as plt
import keyboard

def current_milli_time():
    return datetime.now().strftime('%H:%M:%S.%f')[:-3]

def read_from_port(ser, message_queue, stop_event):
    while not stop_event.is_set():
        if ser.in_waiting:
            response = ser.readline().decode().strip()
            timestamp = current_milli_time()
            print(f"[{timestamp}][{ser.port} IN] {response}")
            message_queue.put((ser.port, timestamp, response))

def send_packet(ser, node_id, length):
    command = f"hirg -r {node_id} -l {length}\n"
    timestamp = current_milli_time()
    print(f"[{timestamp}][{ser.port} OUT] {command.strip()}")
    ser.write(command.encode())

def packet_loss_test(ser, node_id, length, message_queue, results, stop_event):
    packets_sent = 0
    packets_received = 0

    while not stop_event.is_set():
        send_packet(ser, node_id, length)
        packets_sent += 1

        start_time = time.time()
        response_found = False

        while time.time() - start_time < 0.1:  # 100ms timeout
            try:
                _, _, message = message_queue.get(timeout=0.01)
                if "Round-trip latency /us:" in message:
                    packets_received += 1
                    response_found = True
                    break
            except queue.Empty:
                continue

        loss_rate = (packets_sent - packets_received) / packets_sent * 100
        results.append((packets_sent, loss_rate))

        time.sleep(max(0, 0.1 - (time.time() - start_time)))  # Ensure 10Hz sending rate

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
    node = {'port': 'COM5', 'node_id': 853210837}
    packet_length = 50  # You can adjust this value

    try:
        node['serial'] = serial.Serial(node['port'], 115200, timeout=1)
    except serial.SerialException as e:
        print(f"Error opening serial port {node['port']}: {e}")
        return

    message_queue = queue.Queue()
    stop_event = threading.Event()

    read_thread = threading.Thread(target=read_from_port, args=(node['serial'], message_queue, stop_event))
    read_thread.start()

    results = []
    test_thread = threading.Thread(target=packet_loss_test, args=(node['serial'], node['node_id'], packet_length, message_queue, results, stop_event))
    test_thread.start()

    print("Press 'q' to stop the test and plot results...")
    keyboard.wait('q')

    stop_event.set()
    read_thread.join()
    test_thread.join()

    try:
        node['serial'].close()
    except:
        pass
    print("\nSerial port closed")

    if results:
        plot_results(results)
    else:
        print("No results to plot.")

if __name__ == "__main__":
    main()