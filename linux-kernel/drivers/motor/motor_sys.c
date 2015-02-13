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

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
//#include <linux/list.h>
#include <linux/spinlock.h>
#include <linux/device.h>
//#include <linux/timer.h>
#include <linux/err.h>
#include <linux/ctype.h>
#include <linux/motor.h>
#include <linux/hrtimer.h>


//DECLARE_RWSEM(motor_lock);

static struct class motor_class = {
	.name = "motor",
};

static ssize_t motor_type_show(struct device *dev, 
		struct device_attribute *attr, char *buf)
{
	struct motor_classdev *motor_cdev = dev_get_drvdata(dev);

	switch(motor_cdev->type)
	{
		case MOTOR_TYPE_DC:
			sprintf(buf, "dc motor\n");
			break;
		case MOTOR_TYPE_STEPPER:
			sprintf(buf, "stepper motor\n");
			break;
		case MOTOR_TYPE_SERVO:
			sprintf(buf, "servo motor\n");
			break;
		//case MOTOR_TYPE_VCM:
		//	sprintf(buf, "vcm motor\n");
		//	break;
		default:
		case MOTOR_TYPE_UNKNOW:
			sprintf(buf, "unknow\n");
			break;
	}
	return strlen(buf);
}

static ssize_t motor_ctl_store(struct device *dev, struct device_attribute *attr,
			const char *buf, size_t count)
{
	struct motor_classdev *motor_cdev = dev_get_drvdata(dev);
	int para=0;
	char cmd[16];

	memset(cmd,0 ,sizeof(cmd));
	if(!motor_cdev->ctl)
		return -EPERM;

	sscanf(buf, "%s %d", cmd, &para);

	if(!strncmp(cmd,"forward",7))
		motor_cdev->ctl(motor_cdev, MOTOR_FORWARD, ABS(para));
	else if(!strncmp(cmd,"backward",8))
		motor_cdev->ctl(motor_cdev, MOTOR_BACKWARD, ABS(para));
	else if(!strncmp(cmd,"init",4))
		motor_cdev->ctl(motor_cdev, MOTOR_INIT, ABS(para));
	else if(!strncmp(cmd,"mount",5))
		motor_cdev->ctl(motor_cdev, MOTOR_MOUNT, ABS(para));
	else if(!strncmp(cmd,"unmount",7))
		motor_cdev->ctl(motor_cdev, MOTOR_UNMOUNT, ABS(para));
	else if(!strncmp(cmd,"hold",5))
		motor_cdev->ctl(motor_cdev, MOTOR_HOLD, ABS(para));
	else if(!strncmp(cmd,"standby",7))
		motor_cdev->ctl(motor_cdev, MOTOR_STANDBY, 0);
	else
		return  -EPERM;		//cmd error 
	return count;
}



static ssize_t motor_state_show(struct device *dev, 
		struct device_attribute *attr, char *buf)
{
	struct motor_classdev *motor_cdev = dev_get_drvdata(dev);

	if(!motor_cdev->getstate)
		return -EPERM;

	switch(motor_cdev->getstate(motor_cdev))
	{
		case MOTOR_FORWARD:
			sprintf(buf, "forward\n");
			break;
		case MOTOR_BACKWARD:
			sprintf(buf, "backward\n");
			break;
		case MOTOR_MOUNT:
			sprintf(buf, "mount\n");
			break;
		case MOTOR_UNMOUNT:
			sprintf(buf, "unmount\n");
			break;
		case MOTOR_INIT:
			sprintf(buf, "init\n");
			break;
		case MOTOR_HOLD:
			sprintf(buf, "hold\n");
			break;
		case MOTOR_STANDBY:
			sprintf(buf, "standby\n");
			break;
		default:
			sprintf(buf, "unknow\n");
			break;
	}
	return strlen(buf);
}

static ssize_t motor_speed_store(struct device *dev, struct device_attribute *attr,
			const char *buf, size_t count)
{
	struct motor_classdev *motor_cdev = dev_get_drvdata(dev);
	int hz;
	
	sscanf(buf, "%d", &hz);
	if((hz >0) &&(hz <= 5000) && (motor_cdev->setspeed))
	{
		motor_cdev->setspeed(motor_cdev, hz);
		return count;
	}
	else
	{
		return -EPERM;
	}
}



static ssize_t motor_speed_show(struct device *dev, 
		struct device_attribute *attr, char *buf)
{
	struct motor_classdev *motor_cdev = dev_get_drvdata(dev);

	if(motor_cdev->getspeed)
	{
		sprintf(buf, "%d\n", motor_cdev->getspeed(motor_cdev));
		return strlen(buf);
	}
	else
		return -EPERM;
}

static ssize_t motor_pos_store(struct device *dev, struct device_attribute *attr,
			const char *buf, size_t count)
{
	struct motor_classdev *motor_cdev = dev_get_drvdata(dev);
	int pos;
	
	sscanf(buf, "%d", &pos);
	if(motor_cdev->setpos)
	{
		motor_cdev->setpos(motor_cdev, pos);
		return count;
	}
	else
		return -EPERM;
}



static ssize_t motor_pos_show(struct device *dev, 
		struct device_attribute *attr, char *buf)
{
	struct motor_classdev *motor_cdev = dev_get_drvdata(dev);
	
	if(motor_cdev->getpos)
	{
		sprintf(buf, "%d\n", motor_cdev->getpos(motor_cdev));
		return strlen(buf);
	}
	else
		return -EPERM;
}


static struct device_attribute motor_class_attrs[] = {
	__ATTR(type, S_IRUGO, motor_type_show, NULL),
	__ATTR(speed, S_IRUGO|S_IWUGO, motor_speed_show, motor_speed_store),
	__ATTR(ctl, S_IWUGO, NULL, motor_ctl_store),
	__ATTR(state, S_IRUGO, motor_state_show, NULL ),
	__ATTR(pos, S_IRUGO|S_IWUGO, motor_pos_show, motor_pos_store),
	__ATTR_NULL,
};


/**
 * led_classdev_register - register a new object of led_classdev class.
 * @parent: The device to register.
 * @led_cdev: the led_classdev structure for this device.
 */
int motor_classdev_register(struct device *parent, struct motor_classdev *motor_cdev)
{
	motor_cdev->dev = device_create(&motor_class, parent, 0, motor_cdev,
				      "%s", motor_cdev->name);
	if (IS_ERR(motor_cdev->dev))
		return PTR_ERR(motor_cdev->dev);

	printk(KERN_DEBUG "Registered motor device: %s\n",
			motor_cdev->name);

	return 0;
}
EXPORT_SYMBOL_GPL(motor_classdev_register);

/**
 * motor_classdev_unregister - unregisters a object of motor_properties class.
 * @motor_cdev: the motor device to unregister
 *
 * Unregisters a previously registered via motor_classdev_register object.
 */
void motor_classdev_unregister(struct motor_classdev *motor_cdev)
{
	if(motor_cdev->ctl)	
		motor_cdev->ctl(motor_cdev, MOTOR_STANDBY, 0);
	device_unregister(motor_cdev->dev);
}
EXPORT_SYMBOL_GPL(motor_classdev_unregister);


static int motor_suspend(struct device *dev, pm_message_t state)
{
	struct motor_classdev *motor_cdev = dev_get_drvdata(dev);

	if (motor_cdev->flags & MOTOR_SUSPEND_SUPPORT)
	{
		if(motor_cdev->ctl)		motor_cdev->ctl(motor_cdev, MOTOR_STANDBY, 0);
		motor_cdev->flags |= MOTOR_SUSPENDED;
	}
	return 0;
}

static int motor_resume(struct device *dev)
{
	struct motor_classdev *motor_cdev = dev_get_drvdata(dev);

	if (motor_cdev->flags & MOTOR_SUSPEND_SUPPORT)
	{
		//if(motor_cdev->ctl)		motor_cdev->ctl(motor_cdev, motor_cdev->state);
		motor_cdev->flags &= ~MOTOR_SUSPENDED;
	}
	return 0;
}


static int __init motor_init(void)
{
	int result = 0;

	result = class_register(&motor_class);
	if (result) 
	{
		printk("class_register missed\r\n");
		return -1;
	}
	motor_class.suspend = motor_suspend;
	motor_class.resume = motor_resume;
	motor_class.dev_attrs = motor_class_attrs;
	return 0;
}

static void __exit motor_exit(void)
{
	class_unregister(&motor_class);
}

subsys_initcall(motor_init);
module_exit(motor_exit);

MODULE_AUTHOR("Eric Hsiao, erichsiao815@gmail.com");
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Motor sys Interface");

