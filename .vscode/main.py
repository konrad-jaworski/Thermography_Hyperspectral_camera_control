import sys
from machine import Pin, I2C
import time

# ---- (StepperMotor, AS5600 classes) ----

class StepperMotor:
    """
    Class used to control motion of the stepper motors.
    """
    def __init__(self, step_pin, dir_pin, step_delay_us,ms1,ms2,ms3):
        self.STEP = Pin(step_pin, Pin.OUT)
        self.DIR = Pin(dir_pin, Pin.OUT)
        self.step_delay_us = step_delay_us
        self.ms1=Pin(ms1,Pin.OUT)
        self.ms2=Pin(ms2,Pin.OUT)
        self.ms3=Pin(ms3,Pin.OUT)
        self.current_mode="full"
        self.set_microstepping(self.current_mode)

    def single_step(self):
        self.STEP.high()
        time.sleep_us(self.step_delay_us)
        self.STEP.low()
        time.sleep_us(self.step_delay_us)

    def move(self, steps, direction):
        self.DIR.value(direction)  # 0 or 1
        for _ in range(steps):
            self.single_step()

    def set_microstepping(self,mode):
        """
        Set microstepping mode on A4988.
        mode: string like "full", "half", "quarter", "eighth", "sixteenth"
        """
        modes = {
            "full":      (0, 0, 0),
            "half":      (1, 0, 0),
            "quarter":   (0, 1, 0),
            "eighth":    (1, 1, 0),
            "sixteenth": (1, 1, 1),
        }

        if mode not in modes:
            raise ValueError("Invalid stepping mode")


        ms1_val, ms2_val, ms3_val = modes[mode]
        self.ms1.value(ms1_val)
        self.ms2.value(ms2_val)
        self.ms3.value(ms3_val)
        self.current_mode = mode
        # print(f"[Stepper] Microstepping set to: {mode}")
        # print("MS1:", self.ms1.value(), "MS2:", self.ms2.value(), "MS3:", self.ms3.value())

    def calibration(self, encoder, target_angle):
        pos_err=0.2

        def angle_diff(a, b):
            """Returns signed smallest angle from a to b (range -180 to +180)."""
            return (b - a + 540) % 360 - 180

        def shortest_direction(current, target):
            """Returns 1 for clockwise, 0 for counter-clockwise."""
            return 0 if angle_diff(current, target) > 0 else 1

        current_angle = encoder.read_angle_deg()

        while abs(encoder.read_angle_deg()-target_angle)>pos_err:
            direction = shortest_direction(current_angle, target_angle)
            self.move(1,direction)


class AS5600:
    """
    Class which handles read out of the AS5600 encoders 
    """
    def __init__(self, i2c, addr=0x36):
        self.i2c = i2c
        self.addr = addr

    def read_angle_raw(self):
        try:
            raw_data = self.i2c.readfrom_mem(self.addr, 0x0C, 2)
            angle = (raw_data[0] << 8) | raw_data[1]
            return angle & 0x0FFF  # 12-bit mask
        except Exception as e:
            print("I2C read error:", e)
            return None

    def read_angle_deg(self):
        raw = self.read_angle_raw()
        if raw is not None:
            return (raw / 4096) * 360.0
        return None

# ---- (Lower_gear_pos, Upper_gear_pos) ----

Lower_gear_pos=[332,297.5,260.6,225.4,189,152.6,116.7,81.8,8.6]#No filter ,2000, 2150, 2300, 2450, 2600, 2750, 2900, Substrate
Upper_gear_pos=[ 194.6,229,266.4,302.5,339.3,15.3,52.3, 86] #No filter, 500, 800, 950, 1100, 1250, 1400, 1700  


# Class and hardware setup
motors = [StepperMotor(14, 15, 500, 10, 11, 12),    # Top motor
          StepperMotor(17, 16, 500, 6, 7, 8)]       # Bottom motor

motors[0].set_microstepping("sixteenth")
motors[1].set_microstepping("sixteenth")

i2c0 = I2C(0, scl=Pin(1), sda=Pin(0))
i2c1 = I2C(1, scl=Pin(3), sda=Pin(2))

encoder_up = AS5600(i2c0)
encoder_down = AS5600(i2c1)

# Calibration to zero (index 0_0)
motors[1].calibration(encoder_down, Lower_gear_pos[0])
time.sleep(0.5)
motors[0].calibration(encoder_up, Upper_gear_pos[0])
time.sleep(0.5)

# Helper functions
def parse_index(command):
    try:
        parts = command.strip().split()
        if len(parts) == 2 and parts[0] == "ping":
            i_str, j_str = parts[1].split('_')
            return int(i_str), int(j_str)
    except:
        pass
    return None, None

# Initialize last known positions
last_i = -1
last_j = -1

def move_to_position(i, j):
    global last_i, last_j

    if not (0 <= i < len(Upper_gear_pos) and 0 <= j < len(Lower_gear_pos)):
        return False  # invalid index

    # Move top motor if needed
    if i != last_i:
        print(f"Moving TOP motor to index {i} (angle {Upper_gear_pos[i]})")
        motors[0].calibration(encoder_up, Upper_gear_pos[i])
        last_i = i
        time.sleep(0.5)
    else:
        print(f"TOP motor already at index {i}, skipping move")

    # Move bottom motor if needed
    if j != last_j:
        print(f"Moving BOTTOM motor to index {j} (angle {Lower_gear_pos[j]})")
        motors[1].calibration(encoder_down, Lower_gear_pos[j])
        last_j = j
        time.sleep(0.5)
    else:
        print(f"BOTTOM motor already at index {j}, skipping move")

    return True


# ------------------ Main loop ---------------------
print("Pico ready. Waiting for commands...")

while True:
    try:
        line = sys.stdin.readline()
        if not line:
            continue

        i, j = parse_index(line)

        if i is not None and j is not None:
            print(f"Received command: ping {i}_{j}")

            success = move_to_position(i, j)
            if success:
                print(f"<pong {i}_{j}>")
            else:
                print("<error invalid index>")
        else:
            print("<error invalid command>")

    except Exception as e:
        print(f"<error {str(e)}>")
