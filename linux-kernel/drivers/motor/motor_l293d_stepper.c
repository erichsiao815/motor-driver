/*
 * 	motor_l293d_stepper.c
 *
 * Copyright (C) 2015 Eric Hsiao <erichsiao815@gmail.com>
 *
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 * 
 *
 * 2ch is used in one stepper motor controlling:
 *	
 *		1Y : A
 *		2Y : /A
 *		4Y : B
 *		3Y : /B
 *
 *		1-2 EN & 3-4EN connect to one gpio or 5V immediately.
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


#define MOTOR_NAME		"L293D-STEPPER"



struct l293d_stepper_chdata {
	int ch;
	bool use;
	const char *name;
	enum motor_type type;
	enum motor_state state;
	int flag;
	unsigned int pps;
	int pos;
	char seqNum;
	// control pin 
	unsigned pin_ch_en;	//channel enable
	unsigned pin_a;		// A
	unsigned pin_an;		// /A
 	unsigned pin_b;		// B
 	unsigned pin_bn;		// /B
 	// timer & work queue
	struct work_struct work;
	struct hrtimer hrtimer;
	int	maxPos;
	int	minPos;
 };

struct l293d_stepper_platdata {
	int num_ch;
	unsigned pin_chip_en;		// enable pin
	struct l293d_stepper_chdata *data;
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

static void _StepSequence(int seq, struct l293d_stepper_chdata *pchdata)
{
	#define		STEP_SEQ		8
	int stepTable_1_2_phase[STEP_SEQ] =
	{
		0x01,	// 0011
		0x03,	// 0011
		0x02,	// 0011
		0x06,	// 0110
		0x04,	// 0011
		0x0c,	// 1100
		0x08,	// 0011
		0x09,	// 1001
	};
	//printk("step seq %d\r\n",seq );
	if((seq < 0) || (seq >= STEP_SEQ))
	{	//standby
		_motor_gpio_output(pchdata->pin_a,0);
		_motor_gpio_output(pchdata->pin_b,0);
		_motor_gpio_output(pchdata->pin_an,0);
		_motor_gpio_output(pchdata->pin_bn,0);
	}
	else
	{
		_motor_gpio_output(pchdata->pin_a,stepTable_1_2_phase[seq]&0x01? 1:0);		//A
		_motor_gpio_output(pchdata->pin_b,stepTable_1_2_phase[seq]&0x02? 1:0);		//B
		_motor_gpio_output(pchdata->pin_an,stepTable_1_2_phase[seq]&0x04? 1:0);	//AM
		_motor_gpio_output(pchdata->pin_bn,stepTable_1_2_phase[seq]&0x08? 1:0);	//BM
	}
}

static void motor_work_handler(struct work_struct *work)
{
	struct l293d_stepper_chdata *chdata =
	    container_of(work, struct l293d_stepper_chdata, work);

	if(chdata->pos ==0)
	{
		_StepSequence(-1, chdata);
	}
	else
	{
		_StepSequence(chdata->seqNum & (STEP_SEQ-1), chdata );
	}
}

enum hrtimer_restart motor_hrtimer_handler(struct hrtimer *timer)
{
	struct l293d_stepper_chdata *chdata =
	    container_of(timer, struct l293d_stepper_chdata, hrtimer);
	
	if(chdata->pos>0){
		if(chdata->pos< 204000)	chdata->pos--;
		chdata->seqNum--;
	}
	else if(chdata->pos<0)
	{
		if(chdata->pos> -204000)	chdata->pos++;
		chdata->seqNum++;
	}
	//schedule_work(&chdata->work);	
	motor_work_handler(&chdata->work);
	if(chdata->pos!=0)	
	{
		hrtimer_forward_now(timer, ktime_set( 0, 1000000000/chdata->pps));
		return HRTIMER_RESTART;
	}
	else
	{
		return HRTIMER_NORESTART;
	}
}

static struct l293d_stepper_chdata *_get_ch_data(struct motor_classdev *motor_cdev)
{
	int i;
	struct l293d_stepper_platdata *data= motor_cdev->data; 
	struct l293d_stepper_chdata *ch_data = NULL; 
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

static void l293d_stepper_ctl(struct motor_classdev *motor_cdev,enum motor_state ctrl, int step)
{
	struct l293d_stepper_chdata *ch_data = _get_ch_data(motor_cdev); 
	
	if(ch_data != NULL)
	{
		switch(ctrl)
		{
			case MOTOR_FORWARD:
				ch_data->pos= step;
				break;
			case MOTOR_BACKWARD:
				ch_data->pos = -step;
				break;
			default:
			case MOTOR_STANDBY:
				ch_data->pos = 0;
				break;
		}
		//printk("set motor step %d\r\n",ch_data-> pos);
		if(ch_data->pos!=0)
		{
			if(hrtimer_active(&ch_data->hrtimer) == HRTIMER_STATE_INACTIVE )
			{
				hrtimer_start(&ch_data->hrtimer, 
							ktime_set( 0, 50000000 ),		//50msec
							HRTIMER_MODE_REL);
				//motor_work_handler(NULL);
			}
		}
	}
}

static enum motor_state	 l293d_stepper_getstate(struct motor_classdev *motor_cdev)
{
	struct l293d_stepper_chdata *ch_data = _get_ch_data(motor_cdev); 
	if(ch_data->pos> 0)
		return MOTOR_FORWARD;
	else if(ch_data->pos < 0)
		return MOTOR_BACKWARD;
	else
		return MOTOR_STANDBY;
}

static void l293d_stepper_setspeed(struct motor_classdev *motor_cdev,unsigned int speed)
{
	struct l293d_stepper_chdata *ch_data = _get_ch_data(motor_cdev); 
	if((speed >0) &&(speed <= 5000))
	{
		ch_data->pps = speed;
	}
}

static unsigned int l293d_stepper_getspeed(struct motor_classdev *motor_cdev)
{
	struct l293d_stepper_chdata *ch_data = _get_ch_data(motor_cdev); 
	return ch_data->pps;
}


static void l293d_stepper_setpos(struct motor_classdev *motor_cdev,unsigned int pos)
{
	struct l293d_stepper_chdata *pdata = _get_ch_data(motor_cdev); 
	//enum motor_state ctrl;
	int target = pos;

	if (target > pdata->maxPos) 
		target = pdata->maxPos;
	else if(target< pdata->minPos)
		target= pdata->minPos;
	
	pdata->pos = pos;
		
}

static unsigned int l293d_stepper_getpos(struct motor_classdev *motor_cdev)
{
	struct l293d_stepper_chdata *pdata = _get_ch_data(motor_cdev); 

	if(pdata != NULL)
		return pdata->pos;
	else
		return 99999;
}


static int __devinit l293d_stepper_probe(struct platform_device *pdev)
{
	int ret =0;
	int i = 0;
	struct l293d_stepper_platdata *pdata = pdev->dev.platform_data; //dev_get_platdata(&pdev->dev);
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
		unsigned gpio[4];
		if(pdata->data[i].use == 0)
			continue;
		motor_dev[i].name = pdata->data[i].name;
		motor_dev[i].type = pdata->data[i].type;
		motor_dev[i].flags = pdata->data[i].flag;
		motor_dev[i].setspeed	 = l293d_stepper_setspeed;
		motor_dev[i].getspeed = l293d_stepper_getspeed;
		motor_dev[i].ctl		= l293d_stepper_ctl;
		motor_dev[i].getstate	= l293d_stepper_getstate;
		motor_dev[i].setpos 	= l293d_stepper_setpos;
		motor_dev[i].getpos 	= l293d_stepper_getpos;
		ret = motor_classdev_register(&pdev->dev, &motor_dev[i]);
		if (ret) {
			dev_err(&pdev->dev, "failed to register motor %s\n",motor_dev[i].name);
			goto err;
		}
		motor_dev[i].data = pdata;
		
		gpio_request(pdata->data[i].pin_a, "stepper A");
		gpio_request(pdata->data[i].pin_an, "stepper /A");
		gpio_request(pdata->data[i].pin_b, "stepper B");
		gpio_request(pdata->data[i].pin_bn, "stepper /B");
		printk("register motor %s succeeded\r\n",motor_dev[i].name);
		
		INIT_WORK(&pdata->data[i].work, motor_work_handler);
		hrtimer_init(&pdata->data[i].hrtimer, CLOCK_REALTIME, HRTIMER_MODE_REL);
		pdata->data[i].hrtimer.function = motor_hrtimer_handler;
		
		gpio[0] = pdata->data[i].pin_a;
		gpio[1] = pdata->data[i].pin_b;
		gpio[2] = pdata->data[i].pin_an;
		gpio[3] = pdata->data[i].pin_bn;
		_StepSequence(-1, &pdata->data[i]);
	}
	platform_set_drvdata(pdev, motor_dev);
	return 0;
err:
	if (i > 0) {
		for (i = i - 1; i >= 0; i--) {
			if(pdata->data[i].use == 0)
			{
				continue;
			}
			motor_classdev_unregister(&motor_dev[i]);
		}
	}
	printk("register motor failed\r\n");
	return ret;
}

static int __exit l293d_stepper_remove(struct platform_device *pdev)
{
	struct motor_classdev	*motor = platform_get_drvdata(pdev);
	struct l293d_stepper_platdata *pdata = motor->data;	//pdev->dev.platform_data;
	int i = 0;

	printk("ch number %d\r\n",pdata->num_ch);

	for (i = 0; i < pdata->num_ch; i++) 
	{
		if(pdata->data[i].use == 0)
		{
			continue;
		}
		motor_classdev_unregister(&motor[i]);
		printk("motor %s removed \r\n",motor[i].name);
		hrtimer_cancel(&pdata->data[i].hrtimer);
		cancel_work_sync(&pdata->data[i].work);
	}
	kfree(motor);
	return 0;
}

static struct l293d_stepper_chdata l293d_stepper_data[] = 
{
	{
		.ch = 0,
		.use = 1,
		.name = "stepper",
		.type = MOTOR_TYPE_STEPPER,
		//.state = MOTOR_STANDBY,
		.flag = MOTOR_SUSPEND_SUPPORT,
		.pps = 100,		 //TBD
		.pos =0,
		.seqNum = 0,
		//.pin_ch_en =14,
		.pin_a = 18,
		.pin_an = 24,
		.pin_b = 23,
		.pin_bn = 25,
	},

};

static struct l293d_stepper_platdata l293d_stepper_platform_data = 
{
	.num_ch = 1,
	//.pin_chip_en = 4 ,		//enable all channel
	.data = l293d_stepper_data,
};

static struct platform_driver l293d_stepper_platform_driver = {
	.driver = {
		.name = MOTOR_NAME,
		.owner =	THIS_MODULE,
	},
	.probe 	=	l293d_stepper_probe,
	.remove	=	l293d_stepper_remove,
};

struct platform_device *pl293d_stepper_platform_device;


static int motor_dc_init(void)
{
	int status;
	
	pl293d_stepper_platform_device = platform_device_register_simple(MOTOR_NAME, -1, NULL, 0); 
	if (IS_ERR(pl293d_stepper_platform_device))
		goto exit;

	//l293d_platform_data.num_ch = sizeof(l293d_platform_data.data)/sizeof(struct l293d_stepper_chdatal293d_stepper_chdata);
	printk("ch number %d\r\n",l293d_stepper_platform_data.num_ch);
	pl293d_stepper_platform_device->dev.platform_data =  &l293d_stepper_platform_data;
	status = platform_driver_register(&l293d_stepper_platform_driver);
	if (status) {
		pr_err("Unable to register platform driver\n");
		goto exit;
	}
	
	return 0;
exit_unregister:
	pl293d_stepper_platform_device->dev.platform_data = NULL;
	platform_device_unregister( pl293d_stepper_platform_device);
	platform_driver_unregister(&l293d_stepper_platform_driver);
exit:
	return -1;

}

static void motor_dc_exit(void)
{
	//cancel_work_sync(&motor_dc_work);
	platform_driver_unregister(&l293d_stepper_platform_driver);
	pl293d_stepper_platform_device->dev.platform_data = NULL;
	platform_device_unregister( pl293d_stepper_platform_device);
	printk(" GoodBye, %s\n",MOTOR_NAME);
}



module_init( motor_dc_init);
module_exit( motor_dc_exit);

MODULE_AUTHOR("Eric Hsiao, erichsiao815@gmail.com");
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("L293D for stepper motor control");


