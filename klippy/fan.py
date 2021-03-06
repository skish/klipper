# Printer fan support
#
# Copyright (C) 2016  Kevin O'Connor <kevin@koconnor.net>
#
# This file may be distributed under the terms of the GNU GPLv3 license.

FAN_MIN_TIME = 0.1

class PrinterFan:
    def __init__(self, printer, config):
        self.printer = printer
        self.config = config
        self.mcu_fan = None
        self.last_fan_value = 0
        self.last_fan_time = 0.
        self.kick_start_time = config.getfloat('kick_start_time', 0.1)
    def build_config(self):
        pin = self.config.get('pin')
        hard_pwm = self.config.getint('hard_pwm', 128)
        self.mcu_fan = self.printer.mcu.create_pwm(pin, hard_pwm, 0)
    # External commands
    def set_speed(self, print_time, value):
        value = max(0, min(255, int(value*255. + 0.5)))
        if value == self.last_fan_value:
            return
        mcu_time = self.mcu_fan.print_to_mcu_time(print_time)
        mcu_time = max(self.last_fan_time + FAN_MIN_TIME, mcu_time)
        if (value and value < 255
            and not self.last_fan_value and self.kick_start_time):
            # Run fan at full speed for specified kick_start_time
            self.mcu_fan.set_pwm(mcu_time, 255)
            mcu_time += self.kick_start_time
        self.mcu_fan.set_pwm(mcu_time, value)
        self.last_fan_time = mcu_time
        self.last_fan_value = value
