from machine import I2C, Pin
i2c = I2C(0, sda=Pin(4), scl=Pin(5), freq=400000)
print("I2C devices:", [hex(addr) for addr in i2c.scan()])