# motor-subsystem
create a motor subsystem in kernel space

linux-kernel/drivers/
        |-- misc
            |-- pwm-sunxi.c         --> bananapi only
        |-- motor
            |-- motor_sys.c         --> motor sybsystem main file
            |-- motor_l293d_dc.c    --> control dc motor with motor sybsystem (L293D)
            |-- motor_l293d_stepper.c   --> control stepper motor with motor sybsystem (L293D)

