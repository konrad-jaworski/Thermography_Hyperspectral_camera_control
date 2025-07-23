import serial
import subprocess

# Path to your C++ executable
cpp_executable = r"C:\Users\stone\source\repos\Workswell_implementation_mk3\x64\Debug\Workswell_implementation_mk3.exe"

# Set up the serial connection (adjust the port and baud rate as needed)
ser = serial.Serial('COM3', 115200, timeout=1)  # On Linux/Mac, use '/dev/ttyACM0'

print("Listening for button press...")

# Function to check for valid messages
def is_valid_message(data):
    return data.startswith("<") and data.endswith(">")

while True:
    if ser.in_waiting > 0:
        data = ser.readline().decode('utf-8').strip()
        print(f"Received: {data}")
        
        if is_valid_message(data) and data == "<RUN>":
            print("Button pressed! Running C++ executable...")
            try:
                # Run the C++ executable
                subprocess.run(cpp_executable, check=True)
                print("C++ executable finished successfully.")
            except subprocess.CalledProcessError as e:
                print(f"Error running executable: {e}")
            except FileNotFoundError:
                print(f"Executable not found at: {cpp_executable}")