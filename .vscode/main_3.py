import sys
import time
import select

print("Pico USB serial is ready")


"""
Script used to test com comunication
"""

while True:
    if sys.stdin in select.select([sys.stdin], [], [], 0)[0]:
        msg = sys.stdin.readline()
        for i, ch in enumerate(msg):
            print(f"Char[{i}] = {repr(ch)}")  # Debug: see what the Pico receives
        msg = msg.strip()
        print("Received on Pico:", msg)

        if msg == "ping":
            print("pong")
        else:
            print("ack:", msg)