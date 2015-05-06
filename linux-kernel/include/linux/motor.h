/*
 * 	motor-sys.c
 *
 * Copyright (C) 2015 Eric Hsiao <erichsiao815@gmail.com>
 *
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 */

#ifndef __LINUX_MOTOR_H_
#define __LINUX_MOTOR_H_

#include <linux/list.h>
#include <linux/spinlock.h>
#include <linux/rwsem.h>
#include <linux/timer.h>


#define ABS(X) ((X) < 0 ? (-1 * (X)) : (X))

/* Lower 16 bits reflect status */
#define MOTOR_SUSPENDED		(1 << 0)
/* Upper 16 bits reflect control information */
#define MOTOR_SUSPEND_SUPPORT	(1 << 16)

enum motor_type {
	MOTOR_TYPE_UNKNOW	=	0x0000,		// TBD, defalut is unknow
	MOTOR_TYPE_DC			=	0x0001,
	MOTOR_TYPE_STEPPER	=	0x0002,
	MOTOR_TYPE_SERVO		=	0x0004,
	MOTOR_TYPE_VCM		=	0x0008,		//TBD
};


enum motor_state {
	MOTOR_STANDBY,		//standby
	MOTOR_INIT,
	MOTOR_MOUNT,
	MOTOR_UNMOUNT,
	MOTOR_FORWARD,
	MOTOR_BACKWARD,
	MOTOR_HOLD,		// exciting and braking
};

struct motor_classdev {
	const char			*name;
	unsigned int 			type;
	enum motor_state		state;		// motor current state
	unsigned int			flags;		// is SUSPEND needed for motor ? (MOTOR_SUSPEND_SUPPORT)
	void					*data;		// motor data

	struct device		*dev;
	
	void		(*ctl)(struct motor_classdev *motor_cdev,enum motor_state ctrl, int step);
	enum motor_state	(*getstate)(struct motor_classdev *led_cdev);
	void		(*setspeed)(struct motor_classdev *motor_cdev,unsigned int speed);		//unit of dc is duty, stepper is pps/ppm
	unsigned int		(*getspeed)(struct motor_classdev *motor_cdev);
	void		(*setpos)(struct motor_classdev *motor_cdev,unsigned int pos);	
	unsigned int		(*getpos)(struct motor_classdev *motor_cdev);
};

int motor_classdev_register(struct device *parent, struct motor_classdev *motor_cdev);
void motor_classdev_unregister(struct motor_classdev *motor_cdev);

#endif
