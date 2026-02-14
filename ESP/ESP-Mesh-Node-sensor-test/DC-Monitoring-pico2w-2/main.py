# Pico 2 W - PWM Current Control with INA260 Monitoring
# Now with UART bidirectional communication to ESP32-C6
# Receives remote commands from Pi 5 Gateway via BLE->ESP32->UART

from machine import I2C, Pin, PWM, UART
import time
import select

# ============== CONFIGURATION ==============
INA260_ADDR = 0x40
PWM_PIN = 16  
PWM_FREQ = 1000  # 1kHz

# ============== SETUP ==============
# Start with load OFF
load_pin = Pin(PWM_PIN, Pin.OUT, value=1)

# I2C for INA260
i2c = I2C(0, sda=Pin(4), scl=Pin(5), freq=400000)

# UART to ESP32-C6 (GP12=TX, GP13=RX)
uart = UART(0, baudrate=115200, tx=Pin(12), rx=Pin(13))

# Configure INA260: 1024-sample averaging
config = 0x6727
i2c.writeto_mem(INA260_ADDR, 0x00, config.to_bytes(2, 'big'))
time.sleep(0.2)

# PWM control
pwm = PWM(Pin(PWM_PIN))
pwm.freq(PWM_FREQ)
pwm.duty_u16(65535)  # HIGH = load OFF

current_duty = 0
monitor_mode = False  # Flag for continuous monitoring

# Onboard LED for feedback — Pico 2W uses CYW43 WiFi chip for LED,
# which can fail to init and block for hundreds of ms per call.
# Gracefully skip LED if CYW43 is unavailable.
try:
    led = Pin("LED", Pin.OUT)
    led.on()
    _led_ok = True
except Exception:
    _led_ok = False
    print("Note: onboard LED unavailable (CYW43), skipping LED feedback")

def blink_command():
    """Three quick flickers (off-on) to acknowledge command receipt"""
    if not _led_ok:
        return
    for _ in range(3):
        led.off()
        time.sleep(0.05)
        led.on()
        time.sleep(0.05)

def set_duty(percent):
    """Set load power (0-100%), inverted for 2N2222->MOSFET"""
    global current_duty
    percent = max(0, min(100, percent))
    inverted = 100 - percent
    pwm.duty_u16(int(inverted * 65535 / 100))
    current_duty = percent

def read_ina260():
    """Read voltage and current with LED 'dip' feedback"""
    if _led_ok:
        led.off()        # Flicker OFF to show sensor activity
        time.sleep(0.1)  # Brief dip

    data = i2c.readfrom_mem(INA260_ADDR, 0x02, 2)
    voltage = int.from_bytes(data, 'big') * 1.25 / 1000

    data = i2c.readfrom_mem(INA260_ADDR, 0x01, 2)
    raw = int.from_bytes(data, 'big')
    if raw > 32767:
        raw -= 65536
    current = abs(raw * 1.25)  # abs() fixes negative polarity

    if _led_ok:
        led.on()         # Resume solid ON status
    return voltage, current

def uart_send(duty, voltage, current, power=None):
    """Send reading to ESP32-C6 via UART"""
    if power is None:
        power = abs(voltage * current)
    msg = f"D:{duty}%,V:{voltage:.3f}V,I:{current:.2f}mA,P:{power:.1f}mW\n"
    uart.write(msg)
    return msg

def uart_send_status(msg):
    """Send status message to gateway"""
    uart.write(f"STATUS:{msg}\n")

def check_uart_command():
    """Check for incoming command from ESP32 (non-blocking)"""
    if uart.any():
        line = uart.readline()
        if line:
            return line.decode('utf-8').strip()
    return None

def process_command(cmd):
    """Process a command (from local input or remote UART)"""
    global monitor_mode
    cmd = cmd.strip().lower()
    
    if cmd:
        blink_command() # Signal command receipt
    
    if cmd == 's' or cmd == 'stop':
        set_duty(0)
        v, i = read_ina260()
        uart_send(0, v, i)
        uart_send_status("STOPPED")
        print(f"OFF: V={v:.3f}V, I={i:.2f}mA")
        monitor_mode = False
        return True
        
    elif cmd == 'r' or cmd == 'ramp':
        uart_send_status("RAMP_START")
        print("Ramp test:")
        for d in [0, 25, 50, 75, 100]:
            set_duty(d)
            time.sleep(0.5)
            v, i = read_ina260()
            uart_send(d, v, i)
            print(f"  {d:3d}%: V={v:.3f}V, I={i:.2f}mA")
            # Check for stop command during ramp
            stop_cmd = check_uart_command()
            if stop_cmd and stop_cmd.lower() in ['s', 'stop']:
                set_duty(0)
                uart_send_status("RAMP_STOPPED")
                print("Ramp interrupted")
                return True
        set_duty(0)
        uart_send_status("RAMP_DONE")
        return True
        
    elif cmd == 'm' or cmd == 'monitor':
        monitor_mode = True
        uart_send_status("MONITOR_START")
        print("Monitor mode started")
        return True
        
    elif cmd.startswith('duty:') or cmd.isdigit():
        # Handle "DUTY:50" or just "50"
        if cmd.startswith('duty:'):
            duty_val = int(cmd.split(':')[1])
        else:
            duty_val = int(cmd)
        set_duty(duty_val)
        v, i = read_ina260()
        uart_send(current_duty, v, i)
        uart_send_status(f"DUTY_SET:{current_duty}")
        print(f"{current_duty}%: V={v:.3f}V, I={i:.2f}mA")
        return True
        
    elif cmd == 'read' or cmd == 'status':
        v, i = read_ina260()
        uart_send(current_duty, v, i)
        return True
        
    return False

# ============== MAIN ==============
print("=" * 45)
print("  PWM Current Control (Remote Ready)")
print("=" * 45)
print(f"PWM: {PWM_FREQ}Hz on GP{PWM_PIN}")

if INA260_ADDR in i2c.scan():
    print("✓ INA260 OK")
    uart_send_status("PICO_READY")
else:
    print("✗ INA260 NOT FOUND!")
    uart_send_status("PICO_ERROR:NO_INA260")

print("\nLocal: 0-100, r=ramp, m=monitor, s=stop")
print("Remote commands also accepted via UART")
print("-" * 45)

# Use polling for UART and stdin (non-blocking)
import sys
poll = select.poll()
poll.register(uart, select.POLLIN)
poll.register(sys.stdin, select.POLLIN)

last_monitor_time = 0

print("\nReady for commands (local or remote)")
if _led_ok:
    led.on() # Turn ON to signal the script is fully initialized and running

while True:
    try:
        # Poll for any input (UART or keyboard) - 50ms timeout
        events = poll.poll(50)
        
        for obj, event in events:
            if obj == uart:
                # Remote UART command from ESP32
                remote_cmd = check_uart_command()
                if remote_cmd:
                    print(f"Remote: {remote_cmd}")
                    process_command(remote_cmd)
            elif obj == sys.stdin:
                # Local keyboard command
                local_cmd = sys.stdin.readline().strip()
                if local_cmd:
                    if local_cmd == 'q':
                        set_duty(0)
                        print("Goodbye!")
                        raise SystemExit
                    process_command(local_cmd)
        
        # If in monitor mode, send readings periodically
        if monitor_mode:
            now = time.ticks_ms()
            if time.ticks_diff(now, last_monitor_time) > 1000:  # Every 1 second
                v, i = read_ina260()
                p = abs(v * i)
                uart_send(current_duty, v, i, p)
                print(f"  {current_duty:3d}%  |  {v:6.3f}V  |  {i:8.2f}mA  |  {p:7.1f}mW")
                last_monitor_time = now
        
    except KeyboardInterrupt:
        monitor_mode = False
        print("\nMonitor stopped (Ctrl+C)")
    except SystemExit:
        break
    except Exception as e:
        print(f"Error: {e}")