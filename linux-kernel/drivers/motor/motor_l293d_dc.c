/*
 * 	motor_l293d_dc.c
 *
 * Copyright (C) 2015 Eric Hsiao <erichsiao815@gmail.com>
 *
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 * 
 *
 * 2ch is used in dc car controlling:
 *	
 *	right wheel (DC motor) -> 
 *		1Y : DC+
 *		2Y : DC-
 *	left wheel (DC motor) -> 
 *		4Y : DC+
 *		3Y : DC-
 *	speed control (PWM) ->
 *		for speed control, 1-2 EN & 3-4EN connect to one pwm pin,
 *		if you do not need to change car's speed, it can connect to 5V immediately.
 */

#include <linux/init.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/fs.h>
#include <linux/device.h>
#include <linux/major.h>
#include <asm/uaccess.h>
#include <linux/slab.h>
#include <linux/cdev.h>
#include <linux/kdev_t.h>
#include <linux/timer.h>
#include <linux/gpio.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/delay.h>
#include <mach/irqs.h>
#include <linux/workqueue.h>
#include <linux/hrtimer.h>
#include <linux/platform_device.h>
#include <linux/motor.h>
#include <linux/pwm.h>


#define MOTOR_NAME		"L293D-DC"

#define PWM_PERIOD		50000 	//50msec, 20k hz
	


struct motor_l293d_ch_data {
	int ch;
	bool use;
	const char *name;
	enum motor_type type;
	enum motor_state state;
	int flag;
	unsigned int duty;
	int pwmid;
	struct pwm_device *pwm;
	// control pin 
	unsigned pin_ch_en;	//channel enable
	unsigned pin_p;		//positive pin(A)
	unsigned pin_n;		//negative pin(B)
};

struct motor_l293d_platform_data {
	int num_ch;
	struct motor_l293d_ch_data *data;
};

static inline int _motor_gpio_output(unsigned gpio, int value)
{
	if(gpio > 0)
	{
		//printk("%s( %d, %d)\r\n",__func__, gpio, value );
		return gpio_direction_output(gpio,value);
	}
	else
	{
		return -1;
	}
}

static void _motor_dc_ctrl(enum motor_state ctrl, struct motor_l293d_ch_data *chdata)
{
	switch(ctrl)
	{
		case MOTOR_FORWARD:
			pwm_enable(chdata->pwm);
			chdata->state = MOTOR_FORWARD;
			_motor_gpio_output(chdata->pin_p,1);
			_motor_gpio_output(chdata->pin_n,0);
			_motor_gpio_output(chdata->pin_ch_en,1);
			break;
		case MOTOR_BACKWARD:
			pwm_enable(chdata->pwm);
			chdata->state = MOTOR_BACKWARD;
			_motor_gpio_output(chdata->pin_p,0);
			_motor_gpio_output(chdata->pin_n,1);
			_motor_gpio_output(chdata->pin_ch_en,1);
			break;
		default:
		case MOTOR_STANDBY:
			pwm_disable(chdata->pwm);
			chdata->state = MOTOR_STANDBY;
			_motor_gpio_output(chdata->pin_p,0);
			_motor_gpio_output(chdata->pin_n,0);
			_motor_gpio_output(chdata->pin_ch_en,0);
			break;
	}
}

static struct motor_l293d_ch_data *_get_ch_data(struct motor_classdev *motor_cdev)
{
	int i;
	struct motor_l293d_platform_data *data= motor_cdev->data; 
	struct motor_l293d_ch_data *ch_data = NULL; 
	//printk("%s\r\n",motor_cdev->name);
	for (i = 0; i<data->num_ch;i++)
	{
		if (strcmp(motor_cdev->name, data->data[i].name)==0)
		{
			ch_data = &data->data[i];
			break;
		}
	}

	if(i < data->num_ch)
	{
		return ch_data;
	}
	else
	{
		printk("[%s] error\r\n", __func__);
		return NULL;
	}
}
static void motor_dc_ctl(struct motor_classdev *motor_cdev,enum motor_state ctrl, int step)
{
	struct motor_l293d_ch_data *ch_data = _get_ch_data(motor_cdev); 
	//struct motor_l293d_platform_data *motor_data= motor_cdev->data; 
	
	if(ch_data != NULL)
	{
		_motor_dc_ctrl(ctrl, ch_data);
	}
}

static enum motor_state	 motor_dc_getstate(struct motor_classdev *motor_cdev)
{
	struct motor_l293d_ch_data *ch_data = _get_ch_data(motor_cdev); 
	
	return ch_data->state;
}

static void motor_dc_setspeed(struct motor_classdev *motor_cdev,unsigned int duty)
{
	struct motor_l293d_ch_data *ch_data = _get_ch_data(motor_cdev); 
	
	if((duty >=0) &&(duty <= 100))
	{
		ch_data->duty = duty;
		if(ch_data->pwm > 0)
		{
			duty = duty * PWM_PERIOD/ 100;
			pwm_config(ch_data->pwm, duty, PWM_PERIOD);
		}
	}
}

static unsigned int motor_dc_getspeed(struct motor_classdev *motor_cdev)
{
	struct motor_l293d_ch_data *ch_data = _get_ch_data(motor_cdev); 
	
	return ch_data->duty;
}

static int __devinit motor_dc_probe(struct platform_device *pdev)
{
	int ret =0;
	int i = 0;
	struct motor_l293d_platform_data *pdata = pdev->dev.platform_data; //dev_get_platdata(&pdev->dev);
	struct motor_classdev *motor_dev;

	if (pdata == NULL) {
		dev_err(&pdev->dev, "missing platform data\n");
		return -ENODEV;
	}
	
	if (pdata->num_ch< 1 ) {
		dev_err(&pdev->dev, "Invalid channel number %d\n", pdata->num_ch);
		return -EINVAL;
	}

	motor_dev = kzalloc(sizeof(struct motor_classdev) * pdata->num_ch, GFP_KERNEL);
	if (motor_dev == NULL) {
		dev_err(&pdev->dev, "failed to alloc memory\n");
		return -ENOMEM;
	}

	for (i = 0; i < pdata->num_ch; i++) 
	{
		if(pdata->data[i].use == 0)
			continue;
		motor_dev[i].name = pdata->data[i].name;
		motor_dev[i].type = pdata->data[i].type;
		motor_dev[i].flags = pdata->data[i].flag;
		motor_dev[i].setspeed	 = motor_dc_setspeed;
		motor_dev[i].getspeed = motor_dc_getspeed;
		motor_dev[i].ctl		= motor_dc_ctl;
		motor_dev[i].getstate	= motor_dc_getstate;
		ret = motor_classdev_register(&pdev->dev, &motor_dev[i]);
		if (ret) {
			dev_err(&pdev->dev, "failed to register motor %s\n",motor_dev[i].name);
			goto err;
		}
		if(pdata->data[i].pwmid>=0)
		{
			printk("init pwmid %d\n",pdata->data[i].pwmid);
			pdata->data[i].pwm = pwm_request(pdata->data[i].pwmid, MOTOR_NAME);
			if (pdata->data[i].pwm <= 0) {
				dev_err(&pdev->dev, "failed to request pwm error\n");
				goto err;
			}
			pwm_config(pdata->data[i].pwm, PWM_PERIOD, PWM_PERIOD);		//duty cycle = 100%
			pwm_disable(pdata->data[i].pwm);
		}
		motor_dev[i].data = pdata;
		gpio_request(pdata->data[i].pin_n, "dc motor +");
		gpio_request(pdata->data[i].pin_p,"dc motor -");
		gpio_request(pdata->data[i].pin_ch_en, "dc motor speed");
		printk("register motor %s succeeded\r\n",motor_dev[i].name);
	}
	// setting pwm if needed
	platform_set_drvdata(pdev, motor_dev);
	return 0;
err:
	if (i > 0) {
		for (i = i - 1; i >= 0; i--) {
			if(pdata->data[i].use == 0)
				continue;
			motor_classdev_unregister(&motor_dev[i]);
		}
	}
			printk("register motor failed\r\n");
	return ret;
}

static int __exit motor_dc_remove(struct platform_device *pdev)
{
	struct motor_classdev	*motor = platform_get_drvdata(pdev);
	struct motor_l293d_platform_data *pdata = motor->data;	//pdev->dev.platform_data;
	int i = 0;

	//printk("ch number %d\r\n",pdata->num_ch);

	for (i = 0; i < pdata->num_ch; i++) 
	{
		if(pdata->data[i].use == 0)
			continue;
		motor_classdev_unregister(&motor[i]);
		printk("motor %s removed \r\n",motor[i].name);
		if(pdata->data[i].pwm > 0) pwm_free(pdata->data[i].pwm);
	}
	kfree(motor);
	return 0;
}

static struct motor_l293d_ch_data l293d_data[] = 
{
	{
		.ch = 0,
		.use = 1,
		.name = "wheel-right",
		.type = MOTOR_TYPE_DC,
		.state = MOTOR_STANDBY,
		.flag = MOTOR_SUSPEND_SUPPORT,
		.duty = 100,
		.pwmid = 1 ,		//pwm using for all wheel
		//.pin_ch_en =10,
		.pin_p = 9,
		.pin_n = 11,
	},
	{
		.ch = 1,
		.use = 1,
		.name = "wheel-left",
		.type = MOTOR_TYPE_DC,
		.state = MOTOR_STANDBY,
		.flag = MOTOR_SUSPEND_SUPPORT,
		.duty = 100,
		.pwmid = -1 ,		// using same pwm with right wheel 
		//.pin_ch_en =17,
		.pin_p = 27,
		.pin_n = 22,
	},
};

static struct motor_l293d_platform_data l293d_platform_data = 
{
	.num_ch = 2,
	.data = l293d_data,
};

static struct platform_driver motor_dc_platform_driver = {
	.driver = {
		.name = MOTOR_NAME,
		.owner =	THIS_MODULE,
	},
	.probe 	=	motor_dc_probe,
	.remove	=	motor_dc_remove,
};

struct platform_device *pmotor_dc_platform_device;


static int motor_dc_init(void)
{
	int status;
	
	pmotor_dc_platform_device = platform_device_register_simple(MOTOR_NAME, -1, NULL, 0); 
	if (IS_ERR(pmotor_dc_platform_device))
		goto exit;

	//l293d_platform_data.num_ch = sizeof(l293d_platform_data.data)/sizeof(struct motor_l293d_ch_data);
	//printk("ch number %d\r\n",l293d_platform_data.num_ch);
	pmotor_dc_platform_device->dev.platform_data =  &l293d_platform_data;
	//platform_set_drvdata(pmotor_dc_platform_device->dev, &l293d_platform_data);
	status = platform_driver_register(&motor_dc_platform_driver);
	if (status) {
		pr_err("Unable to register platform driver\n");
		goto exit_unregister;
	}
	
	return 0;
exit_unregister:
	pmotor_dc_platform_device->dev.platform_data = NULL;
	platform_device_unregister( pmotor_dc_platform_device);
exit:
	return -1;

}

static void motor_dc_exit(void)
{
	platform_driver_unregister(&motor_dc_platform_driver);
	pmotor_dc_platform_device->dev.platform_data = NULL;
	platform_device_unregister( pmotor_dc_platform_device);
	printk(" GoodBye, %s\n",MOTOR_NAME);
}



module_init( motor_dc_init);
module_exit( motor_dc_exit);

MODULE_AUTHOR("Eric Hsiao, erichsiao815@gmail.com");
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("L293D for 2 dc motor (dc car) control");


