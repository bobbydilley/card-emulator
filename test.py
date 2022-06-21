import serial

s = serial.Serial("/dev/ttyUSB0", baudrate=2000000, parity=serial.PARITY_NONE, bytesize = serial.EIGHTBITS, stopbits=serial.STOPBITS_ONE)

while True:
    print(s.read(1).hex())
