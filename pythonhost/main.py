import serial
import serial.tools.list_ports
import threading
import time
import requests
import queue
import tkinter as tk
from flask import Flask, request, jsonify

# --- Configuration ---
ARDUINO_PORT = 'COM7'  # *** IMPORTANT: Change this to your Arduino's COM port ***
BAUD_RATE = 9600
REMOTE_HTTP_ENDPOINT_GPIO7 = 'http://localhost:1500/api/start?mode=print&password=6ORo0vO24goeFw_Q'  # Replace with your actual endpoint
CREDIT_DISPLAY_X = 920 # X coordinate for credit overlay
CREDIT_DISPLAY_Y = 1030 # Y coordinate for credit overlay
FONT_SIZE = 48
FONT_COLOR = "lime green"
BG_COLOR = "black"
OVERLAY_WIDTH = 700
OVERLAY_HEIGHT = 80

# --- Default Konami Sequence ---
DEFAULT_KONAMI_SEQUENCE = "LRUDLRDULR" # Example 10-button sequence: Left, Right, Up, Down, Left, Right, Down, Up, Left, Right

# --- Global Serial Object ---
ser = None

# --- Thread-Safe Queue for UI Updates ---
ui_update_queue = queue.Queue()

# --- Tkinter GUI Globals ---
root = None
credit_label = None

# --- Flask App Setup ---
app = Flask("__Flask__")

# --- Serial Communication Thread Function ---
def serial_reader_thread():
    global ser
    print("Serial reader thread started.")
    while True:
        if ser and ser.is_open:
            try:
                if ser.in_waiting > 0:
                    line = ser.readline().decode('utf-8').strip()
                    print(f"Received from Arduino: {line}")

                    if line.startswith("CREDIT_UPDATE:"):
                        try:
                            credit_str = line.split(":")[1]
                            # Put the credit value into the queue for Tkinter to process
                            ui_update_queue.put(f"Current Credit: {credit_str}€")
                        except IndexError:
                            print(f"Error parsing credit update: {line}")
                    elif line == "GPIO7_TRIGGERED":
                        print(f"GPIO 7 triggered. Calling remote HTTP endpoint: {REMOTE_HTTP_ENDPOINT_GPIO7}")
                        try:
                            response = requests.get(REMOTE_HTTP_ENDPOINT_GPIO7)
                            print(f"Remote endpoint response: {response.status_code} - {response.text}")
                        except requests.exceptions.RequestException as e:
                            print(f"Error calling remote endpoint for GPIO7: {e}")
                    # Add more elif conditions for other Arduino messages if needed
                time.sleep(0.01) # Small delay to prevent busy-waiting
            except serial.SerialException as e:
                print(f"Serial error in reader thread: {e}")
                ser = None # Mark as disconnected
                print("Attempting to re-establish serial connection...")
                time.sleep(2) # Wait before retrying
                setup_serial() # Try to reconnect
            except Exception as e:
                print(f"Unexpected error in serial reader thread: {e}")
                time.sleep(1)
        else:
            time.sleep(1) # Wait if serial port is not open or not set up

# --- Serial Setup Function ---
def setup_serial():
    global ser
    ports = serial.tools.list_ports.comports()
    arduino_port_found = False
    for p in ports:
        if ARDUINO_PORT in p.device:
             arduino_port_found = True
             print(f"Found Arduino at {p.device}")
             try:
                 ser = serial.Serial(p.device, BAUD_RATE, timeout=1) # timeout for readline
                 print(f"Serial port {p.device} opened successfully.")
                 # Start the serial reader thread if not already running
                 if not any(thread.name == "serial_reader" for thread in threading.enumerate()):
                     threading.Thread(target=serial_reader_thread, name="serial_reader", daemon=True).start()

                 # Send default sequence to Arduino on connect
                 print(f"Sending initial sequence '{DEFAULT_KONAMI_SEQUENCE}' to Arduino...")
                 ser.write(f"SET_SEQUENCE:{DEFAULT_KONAMI_SEQUENCE}\n".encode('utf-8'))
                 time.sleep(0.1) # Give Arduino a moment to process the initial command
                 return True
             except serial.SerialException as e:
                 print(f"Could not open serial port {p.device}: {e}")
                 ser = None
                 return False
    if not arduino_port_found:
        print(f"Arduino not found on port {ARDUINO_PORT}. Available ports: {[p.device for p in ports]}")
        print("Please check your Arduino connection and COM port.")
    return False

# --- Flask HTTP Endpoints ---

@app.route('/arduino/lock_count', methods=['POST'])
def lock_arduino_count():
    global ser
    if not ser or not ser.is_open:
        return jsonify({"status": "error", "message": "Arduino not connected or serial port not open."}), 500
    try:
        ser.write(b"LOCK_COUNT\n")
        print("Sent to Arduino: LOCK_COUNT")
        return jsonify({"status": "success", "message": "Lock count command sent to Arduino."})
    except serial.SerialException as e:
        print(f"Error writing to serial port: {e}")
        return jsonify({"status": "error", "message": f"Error communicating with Arduino: {e}"}), 500

@app.route('/arduino/unlock_count', methods=['POST'])
def unlock_arduino_count():
    global ser
    if not ser or not ser.is_open:
        return jsonify({"status": "error", "message": "Arduino not connected or serial port not open."}), 500
    try:
        ser.write(b"UNLOCK_COUNT\n")
        print("Sent to Arduino: UNLOCK_COUNT")
        return jsonify({"status": "success", "message": "Unlock count command sent to Arduino."})
    except serial.SerialException as e:
        print(f"Error writing to serial port: {e}")
        return jsonify({"status": "error", "message": f"Error communicating with Arduino: {e}"}), 500

@app.route('/arduino/reset_credit', methods=['POST'])
def reset_arduino_credit():
    global ser
    if not ser or not ser.is_open:
        return jsonify({"status": "error", "message": "Arduino not connected or serial port not open."}), 500
    try:
        ser.write(b"RESET_CREDIT\n")
        print("Sent to Arduino: RESET_CREDIT")
        return jsonify({"status": "success", "message": "Reset credit command sent to Arduino."})
    except serial.SerialException as e:
        print(f"Error writing to serial port: {e}")
        return jsonify({"status": "error", "message": f"Error communicating with Arduino: {e}"}), 500

@app.route('/arduino/set_sequence', methods=['POST'])
def set_arduino_sequence():
    global ser
    if not ser or not ser.is_open:
        return jsonify({"status": "error", "message": "Arduino not connected or serial port not open."}), 500

    data = request.json
    if not data or 'sequence' not in data:
        return jsonify({"status": "error", "message": "Missing 'sequence' in request body."}), 400

    new_sequence = data['sequence'].strip().upper() # Ensure uppercase consistency
    # Basic validation for sequence characters and length
    if not all(c in 'LURD' for c in new_sequence):
        return jsonify({"status": "error", "message": "Invalid characters in sequence. Only L, U, R, D are allowed."}), 400
    if not (1 <= len(new_sequence) <= 20): # Arbitrary reasonable length limit for safety
        return jsonify({"status": "error", "message": "Sequence length must be between 1 and 20 characters."}), 400

    command = f"SET_SEQUENCE:{new_sequence}\n"
    try:
        ser.write(command.encode('utf-8'))
        print(f"Sent to Arduino: {command.strip()}")
        return jsonify({"status": "success", "message": f"Sequence '{new_sequence}' sent to Arduino."})
    except serial.SerialException as e:
        print(f"Error writing to serial port: {e}")
        return jsonify({"status": "error", "message": f"Error communicating with Arduino: {e}"}), 500

# --- NEW: Endpoint to set current credit ---
@app.route('/arduino/set_credit', methods=['POST'])
def set_arduino_credit():
    global ser
    if not ser or not ser.is_open:
        return jsonify({"status": "error", "message": "Arduino not connected or serial port not open."}), 500

    data = request.json
    if not data or 'credit' not in data:
        return jsonify({"status": "error", "message": "Missing 'credit' in request body."}), 400

    try:
        new_credit = float(data['credit'])
        if new_credit < 0:
            return jsonify({"status": "error", "message": "Credit value cannot be negative."}), 400
        
        # Format to two decimal places for consistency with Arduino's float handling
        command = f"SET_CREDIT:{new_credit:.2f}\n"
        ser.write(command.encode('utf-8'))
        print(f"Sent to Arduino: {command.strip()}")
        return jsonify({"status": "success", "message": f"Credit set to {new_credit:.2f} on Arduino."})
    except ValueError:
        return jsonify({"status": "error", "message": "Invalid credit value. Must be a number."}), 400
    except serial.SerialException as e:
        print(f"Error writing to serial port: {e}")
        return jsonify({"status": "error", "message": f"Error communicating with Arduino: {e}"}), 500
# --- END NEW ---

@app.route('/')
def index():
    return "Arduino Coin Machine Bridge is running."

# --- Tkinter Overlay Functions ---
def setup_overlay():
    global root, credit_label
    root = tk.Tk()
    root.title("Credit Display")

    # Make window always on top, no title bar, and transparent background
    root.wm_attributes("-topmost", True)
    root.overrideredirect(True) # Removes window decorations (border, title bar)
    root.wm_attributes("-transparentcolor", BG_COLOR) # Makes BG_COLOR transparent

    # Set window position and size
    root.geometry(f"{OVERLAY_WIDTH}x{OVERLAY_HEIGHT}+{CREDIT_DISPLAY_X}+{CREDIT_DISPLAY_Y}")

    # Create a label for the credit text
    credit_label = tk.Label(root, text="Loading...",
                            font=("Helvetica", FONT_SIZE),
                            fg=FONT_COLOR,
                            bg=BG_COLOR)
    credit_label.pack(expand=True, fill="both")

    # Periodically check the queue for updates
    root.after(100, update_overlay_from_queue) # Check every 100ms

def update_overlay_from_queue():
    try:
        # Get all items currently in the queue
        while True:
            text_to_display = ui_update_queue.get_nowait()
            credit_label.config(text=text_to_display)
    except queue.Empty:
        pass # No updates in the queue
    finally:
        root.after(100, update_overlay_from_queue) # Schedule next check

# --- Main Application Execution ---
if __name__ == '__main__':
    print("Starting Arduino-HTTP-Overlay Bridge...")

    # Set up Tkinter overlay in the main thread
    setup_overlay()

    # Attempt to set up serial
    if setup_serial():
        print("Serial setup successful.")

        # Run Flask app in a separate thread
        flask_thread = threading.Thread(target=lambda: app.run(host='0.0.0.0', port=5000, debug=False, use_reloader=False), daemon=True)
        flask_thread.start()
        print("Flask server started in a separate thread.")

        # Start the Tkinter main loop (this is blocking and must be in the main thread)
        root.mainloop()
    else:
        print("Failed to set up serial connection. Please resolve the issue and restart.")
        # If serial setup fails, destroy the tkinter window and exit
        if root:
            root.destroy()