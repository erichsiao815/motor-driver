
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


#define MOTOR_NAME		"28BYJ-48"

#define		STEP_SEQ		4

#define	MOTOR_AP_PIN			18
#define	MOTOR_BP_PIN			23
#define	MOTOR_AM_PIN			24
#define	MOTOR_BM_PIN			25

struct hrtimer motor_28byj_hrtimer;

int motor_28byj_Step =0;

int stepTable_2_phase[STEP_SEQ] =
{
	0x03,	// 0011
	0x06,	// 0110
	0x0c,	// 1100
	0x09,	// 1001
};
	
char motor_28byj_stepNum = 0;
unsigned int motor_28byj_pps = 200;


static void motor_28byj_StepSequence(int seq)
{
	if((seq < 0) || (seq >= STEP_SEQ))
	{	//standby
		gpio_direction_output(MOTOR_AP_PIN,0);
		gpio_direction_output(MOTOR_BP_PIN,0);
		gpio_direction_output(MOTOR_AM_PIN,0);
		gpio_direction_output(MOTOR_BM_PIN,0);
	}
	else
	{
		gpio_direction_output(MOTOR_AP_PIN,stepTable_2_phase[seq]&0x01? 1:0);
		gpio_direction_output(MOTOR_BP_PIN,stepTable_2_phase[seq]&0x02? 1:0);
		gpio_direction_output(MOTOR_AM_PIN,stepTable_2_phase[seq]&0x04? 1:0);
		gpio_direction_output(MOTOR_BM_PIN,stepTable_2_phase[seq]&0x08? 1:0);
	}
}

struct work_struct motor_28byj_work;
static void motor_28byj_handler(struct work_struct *work)
{
	if(motor_28byj_Step==0)
	{
		motor_28byj_StepSequence(-1);
	}
	else
	{
		motor_28byj_StepSequence(motor_28byj_stepNum & (STEP_SEQ-1));
	}
}


enum hrtimer_restart motor_28byj_moving(struct hrtimer *timer)
{
	if(motor_28byj_Step>0){
		if(motor_28byj_Step< 204000)	motor_28byj_Step--;
		motor_28byj_stepNum--;
	}
	else if(motor_28byj_Step<0)
	{
		if(motor_28byj_Step> -204000)	motor_28byj_Step++;
		motor_28byj_stepNum++;
	}
	schedule_work(&motor_28byj_work);	
	//motor_handler(NULL);
	if(motor_28byj_Step!=0)	
	{
		hrtimer_forward_now(&motor_28byj_hrtimer, ktime_set( 0, 1000000000/motor_28byj_pps ));
		return HRTIMER_RESTART;
	}
	else
	{
		return HRTIMER_NORESTART;
	}
}


#ifdef CONFIG_MOTOR_SYS_28BYJ_48
static void motor_28byj_ctl(struct motor_classdev *motor_cdev,enum motor_state ctrl, int step)
{
	//int step=204000;

	switch(ctrl)
	{
		case MOTOR_FORWARD:
			motor_28byj_Step = step;
			break;
		case MOTOR_BACKWARD:
			motor_28byj_Step = -step;
			break;
		default:
		case MOTOR_STANDBY:
			motor_28byj_Step = 0;
			break;
	}
		
	if(motor_28byj_Step!=0)
	{
		ktime_t itv_time = ktime_set( 1, 0 );
		if(hrtimer_active(&motor_28byj_hrtimer) == HRTIMER_STATE_INACTIVE )
		{
			hrtimer_start(&motor_28byj_hrtimer, itv_time, HRTIMER_MODE_REL);
			motor_28byj_handler(NULL);
		}
	}
}

static enum motor_state	 motor_28byj_getstate(struct motor_classdev *led_cdev)
{
	if(motor_28byj_Step > 0)
		return MOTOR_FORWARD;
	else if(motor_28byj_Step < 0)
		return MOTOR_BACKWARD;
	else
		return MOTOR_STANDBY;
}


static void motor_28byj_setspeed(struct motor_classdev *motor_cdev,unsigned int speed)
{
	if((speed >0) &&(speed <= 5000))
	{
		motor_28byj_pps = speed;
	}
}

static unsigned int motor_28byj_getspeed(struct motor_classdev *motor_cdev)
{
	return motor_28byj_pps;
}

static struct motor_classdev		motor_28byj_classdev =
{
	.name	= MOTOR_NAME,
	.type	= MOTOR_TYPE_STEPPER,
	.flags	= MOTOR_SUSPEND_SUPPORT,
	.setspeed	= motor_28byj_setspeed,
	.getspeed	= motor_28byj_getspeed,
	.ctl		= motor_28byj_ctl,
	.getstate	= motor_28byj_getstate,
};

static int __devinit motor_28byj_probe(struct platform_device *pdev)
{
	int ret =0;

	ret = motor_classdev_register(&pdev->dev, &motor_28byj_classdev);
	if (ret) {
		dev_err(&pdev->dev, "failed to register motor %s\n",motor_28byj_classdev.name);
		goto err;
	}
	platform_set_drvdata(pdev, &motor_28byj_classdev);
	printk("register motor %s succeeded\r\n",motor_28byj_classdev.name);

	return 0;
err:
	printk("register motor %s failed\r\n",motor_28byj_classdev.name);
	return ret;
}

static int __exit motor_28byj_remove(struct platform_device *pdev)
{
	struct motor_classdev	*motor = platform_get_drvdata(pdev);

	motor_classdev_unregister(motor);

	printk(" motor removed\n");
	return 0;
}
static struct platform_driver motor_28byj_driver = {
	.driver = {
		.name = MOTOR_NAME,
		.owner =	THIS_MODULE,
	},
	.probe 	=	motor_28byj_probe,
	.remove	=	motor_28byj_remove,
};

struct platform_device *pmotor_28byj_dev;

#else
static ssize_t motor_28byj_ctl_store(struct class *class, struct class_attribute *attr,
			const char *buf, size_t count)
{
	int step=0;
	
	sscanf(buf, "%d", & step);

	motor_28byj_Step = step;
	if(motor_28byj_Step!=0)
	{
		ktime_t itv_time = ktime_set( 1, 0 );
		if(hrtimer_active(&motor_28byj_hrtimer) == HRTIMER_STATE_INACTIVE )
		{
			hrtimer_start(&motor_28byj_hrtimer, itv_time, HRTIMER_MODE_REL);
			motor_28byj_handler(NULL);
		}
	}
	return count;
}



static ssize_t motor_28byj_state_show(struct class *class, struct class_attribute *attr, 
			char *buf)
{
	//printk("In %s function\n",__func__);
	if(motor_28byj_Step > 0)
		sprintf(buf, "forward %d\n", (int)abs(motor_28byj_Step));
	else if(motor_28byj_Step < 0)
		sprintf(buf, "backward %d\n", (int)abs(motor_28byj_Step));
	else
		sprintf(buf, "standby\n");
	
	return strlen(buf);
}

static ssize_t motor_28byj_frequence_store(struct class *class, struct class_attribute *attr,
			const char *buf, size_t count)
{
	int hz;
	
	sscanf(buf, "%d", &hz);
	if((hz >0) &&(hz <= 5000))
	{
		motor_28byj_pps = hz;
	}
	return count;
}



static ssize_t motor_28byj_frequence_show(struct class *class, struct class_attribute *attr, 
			char *buf)
{
	sprintf(buf, "%d\n", motor_28byj_pps);
	
	return strlen(buf);
}



static struct class_attribute motor_28byj_class_attr[] =
{ 
	__ATTR(motor, S_IRUGO| S_IWUGO, motor_28byj_state_show, motor_28byj_ctl_store),
	__ATTR(hz, S_IRUGO| S_IWUGO, motor_28byj_frequence_show, motor_28byj_frequence_store),
	__ATTR_NULL,
};

static struct class motor_28byj_drv =
{
	.name = MOTOR_NAME,
	.owner = THIS_MODULE,
	.class_attrs = (struct class_attribute *) &motor_28byj_class_attr,
};
#endif

static int motor_28byj_init(void)
{
	int status;
	
#ifdef CONFIG_MOTOR_SYS_28BYJ_48
	pmotor_28byj_dev = platform_device_register_simple(MOTOR_NAME, -1, NULL, 0); 
	if (IS_ERR(pmotor_28byj_dev))
		goto exit;

	status = platform_driver_register(&motor_28byj_driver);
	if (status) {
		pr_err("Unable to register platform driver\n");
		goto exit;
	}
#else
	status = class_register(&motor_28byj_drv);
	if (status < 0)
	{
		printk("Registering Class Failed\n");
		goto exit;
	}
#endif

	gpio_request(MOTOR_AP_PIN, "motor A+ test");
	gpio_request(MOTOR_BP_PIN, "motor B+ test");
	gpio_request(MOTOR_AM_PIN, "motor A- test");
	gpio_request(MOTOR_BM_PIN, "motor B- test");
	if (status < 0)
	{
		printk("gpio request Failed\n");
		goto exit_unregister;
	}
	motor_28byj_StepSequence(-1);
	INIT_WORK(&motor_28byj_work, motor_28byj_handler);
	
	hrtimer_init(&motor_28byj_hrtimer, CLOCK_REALTIME, HRTIMER_MODE_REL);
	motor_28byj_hrtimer.function = motor_28byj_moving;
	return 0;
exit_unregister:
	platform_device_unregister( pmotor_28byj_dev);
	platform_driver_unregister(&motor_28byj_driver);
exit:
	return -1;

}


static void motor_28byj_exit(void)
{
	motor_28byj_StepSequence(-1);
	gpio_free(MOTOR_AP_PIN);
	gpio_free(MOTOR_BP_PIN);
	gpio_free(MOTOR_AM_PIN);
	gpio_free(MOTOR_BM_PIN);
	hrtimer_cancel(&motor_28byj_hrtimer);
	cancel_work_sync(&motor_28byj_work);
#ifdef CONFIG_MOTOR_SYS_28BYJ_48
	platform_device_unregister( pmotor_28byj_dev);
	platform_driver_unregister(&motor_28byj_driver);
#else
	class_unregister(&motor_28byj_drv);
#endif 
	printk(" GoodBye, %s\n",MOTOR_NAME);
}

module_init( motor_28byj_init);
module_exit( motor_28byj_exit);

MODULE_AUTHOR("Eric Hsiao, erichsiao815@gmail.com");
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("28BYJ-48 stepper motor control");


