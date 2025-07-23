from machine import Pin,I2C
import time

class StepperMotor:
    """
    This class bla bla bla
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
    

class thermography_setup:
    def __init__(self,relay_pin):
        self.relay=Pin(relay_pin,Pin.OUT)


    # Function to send a message with start and end markers
    def send_message(self,message):
        print(f"<{message}>")  # Send message with markers


    def halogen_lamp(self,period_time):
        self.relay.high()  # Turn ON the relay
        time.sleep(period_time)   # Wait for X [seconds]
        self.relay.low()  # Turn OFF the relay

    def demo(self,motor_top,motor_bottom,top_filter,bottom_filter,encoder_top,encoder_bottom):
        for i in range(top_filter):
            motor_top.calibration(encoder_top,Upper_gear_pos[i])
            time.sleep_ms(300)
            for j in range(bottom_filter):
                motor_bottom.calibration(encoder_bottom,Lower_gear_pos[j])
                time.sleep_ms(300)


    def taking_photo(self,motor_top,motor_bottom,top_filter,bottom_filter,encoder_top,encoder_bottom):
        
        top_index=0
        bottom_index=0
        for i in range(top_filter*bottom_filter+top_filter+bottom_filter+1):
            if bottom_index==(bottom_filter+1):
                bottom_index=0
                top_index=top_index+1

                motor_top.calibration(encoder_top,Upper_gear_pos[top_index])
                time.sleep(0.5)

            motor_bottom.calibration(encoder_bottom,Lower_gear_pos[bottom_index])
            time.sleep(0.5)
            print(f'Current combination: Top: {top_index} / Bottom: {bottom_index}')
            print(f'We are at: {i+1} out of {top_filter*bottom_filter+top_filter+bottom_filter+1}')

            time.sleep(5) # Time for add on to the finish changing its position 5 [s]
            self.send_message("RUN")  # Send "RUN" with markers
            time.sleep(30)  # Time for aquisition by the camera 30 [s]

            bottom_index=bottom_index+1



#------------------------------------------------------------------ Class instance initialization-----------------

hyperspectral_mode=True
thermography_mode=False

period_time=20 #Time of halogen lamps flash [s]
time_for_camera_initialization = 30#Time required for camera initialization [s] (Value for 2500 frames is 30) 

# Trigger pin for the starting whole sequence
button=Pin(18,Pin.IN,Pin.PULL_UP)

# Config of the setup
STEP_DELAY_US=500 # Delay between HIGH and LOW (in us)

# Motors initialization
motors=[StepperMotor(14,15,STEP_DELAY_US,10,11,12), # Upper motor
        StepperMotor(17,16,STEP_DELAY_US,6,7,8) # Lower motor
]

motors[0].set_microstepping("sixteenth") 
motors[1].set_microstepping("sixteenth")

# First AS5600 on I2C0 (e.g. GP0 = SDA, GP1 = SCL)
i2c0 = I2C(0, scl=Pin(1), sda=Pin(0))
encoder_up = AS5600(i2c0) # Upper encoder


# Second AS5600 on I2C1 (e.g. GP2 = SDA, GP3 = SCL)
i2c1 = I2C(1, scl=Pin(3), sda=Pin(2))
encoder_down = AS5600(i2c1) # Lower encoder


# Initialization of setup operation
thermo_setup=thermography_setup(13)


#------------------------------------------------------Absolute positions for the camera
Lower_gear_pos=[332,297.5,260.6,225.4,189,152.6,116.7,81.8,8.6]#No filter ,2000, 2150, 2300, 2450, 2600, 2750, 2900, Substrate
Upper_gear_pos=[ 194.6,229,266.4,302.5,339.3,15.3,52.3, 86] #No filter, 500, 800, 950, 1100, 1250, 1400, 1700  



# -------------------------------------------- Calibration sequence --------------------------
motors[1].calibration(encoder_down,Lower_gear_pos[0]) # Lower motor calibration
time.sleep(0.5) 
motors[0].calibration(encoder_up,Upper_gear_pos[0]) # Upper motor calibration
time.sleep(0.5)

angle_up = encoder_up.read_angle_deg() # Upper encoder
angle_down = encoder_down.read_angle_deg() # Lower encoder    
print("Upper encoder 1:", angle_up, "Lower encoder 2:", angle_down)


# --------------------------------------------------------------Main loop----------------------
while True:  

    if button.value() == 0:  # Button is pressed (assuming active-low)
        
        if hyperspectral_mode:
            thermo_setup.taking_photo(motors[0],motors[1],7,7,encoder_up,encoder_down)
            print('We are finished with taking photo!')
        elif thermography_mode:
            thermo_setup.send_message("RUN")  # Send "RUN" with markers
            time.sleep(time_for_camera_initialization)  # Debounce delay
            thermo_setup.halogen_lamp(period_time)

        
        
        
        
