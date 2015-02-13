
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


#define MOTOR_NAME		"DC"


#define	MOTOR_P_PIN			17
#define	MOTOR_M_PIN			27
#define	MOTOR_PWM_PIN		4


int motor_dc_Step =0;
	
unsigned int motor_dtuty= 100;

struct work_struct motor_dc_work;
static void _motor_dc_ctrl(enum motor_state ctrl)
{
	switch(ctrl)
	{
		case MOTOR_FORWARD:
			gpio_direction_output(MOTOR_P_PIN,1);
			gpio_direction_output(MOTOR_M_PIN,0);
			gpio_direction_output(MOTOR_PWM_PIN,1);
			break;
		case MOTOR_BACKWARD:
			gpio_direction_output(MOTOR_P_PIN,0);
			gpio_direction_output(MOTOR_M_PIN,1);
			gpio_direction_output(MOTOR_PWM_PIN,1);
			break;
		default:
		case MOTOR_STANDBY:
			gpio_direction_output(MOTOR_P_PIN,0);
			gpio_direction_output(MOTOR_M_PIN,0);
			gpio_direction_output(MOTOR_PWM_PIN,0);
			break;
	}
}

static void motor_dc_ctl(struct motor_classdev *motor_cdev,enum motor_state ctrl)
{
	_motor_dc_ctrl(ctrl);
}

static enum motor_state	 motor_dc_getstate(struct motor_classdev *led_cdev)
{
	if(__gpio_get_value(MOTOR_PWM_PIN) && __gpio_get_value(MOTOR_P_PIN))
		return MOTOR_FORWARD;
	else if(__gpio_get_value(MOTOR_PWM_PIN) && __gpio_get_value(MOTOR_M_PIN))
		return MOTOR_BACKWARD;
	else
		return MOTOR_STANDBY;
}

static void motor_dc_setspeed(struct motor_classdev *motor_cdev,unsigned int speed)
{
	if((speed >0) &&(speed <= 100))
	{
		motor_dtuty = speed;
	}
}

static unsigned int motor_dc_getspeed(struct motor_classdev *motor_cdev)
{
	return motor_dtuty;
}

static struct motor_classdev		motor_dc_classdev =
{
	.name	= MOTOR_NAME,
	.type	= MOTOR_TYPE_DC,
	.flags	= MOTOR_SUSPEND_SUPPORT,
	.setspeed	= motor_dc_setspeed,
	.getspeed	= motor_dc_getspeed,
	.ctl		= motor_dc_ctl,
	.getstate	= motor_dc_getstate,
};

static int __devinit motor_dc_probe(struct platform_device *pdev)
{
	int ret =0;

	ret = motor_classdev_register(&pdev->dev, &motor_dc_classdev);
	if (ret) {
		dev_err(&pdev->dev, "failed to register motor %s\n",motor_dc_classdev.name);
		goto err;
	}
	platform_set_drvdata(pdev, &motor_dc_classdev);
	printk("register motor %s succeeded\r\n",motor_dc_classdev.name);

	return 0;
err:
	printk("register motor %s failed\r\n",motor_dc_classdev.name);
	return ret;
}

static int __exit motor_dc_remove(struct platform_device *pdev)
{
	struct motor_classdev	*motor = platform_get_drvdata(pdev);

	motor_classdev_unregister(motor);

	printk(" motor removed\n");
	return 0;
}

static struct platform_driver motor_dc_driver = {
	.driver = {
		.name = MOTOR_NAME,
		.owner =	THIS_MODULE,
	},
	.probe 	=	motor_dc_probe,
	.remove	=	motor_dc_remove,
};

struct platform_device *pmotor_dc_dev;


static int motor_dc_init(void)
{
	int status;
	
	pmotor_dc_dev = platform_device_register_simple(MOTOR_NAME, -1, NULL, 0); 
	if (IS_ERR(pmotor_dc_dev))
		goto exit;

	status = platform_driver_register(&motor_dc_driver);
	if (status) {
		pr_err("Unable to register platform driver\n");
		goto exit;
	}

	gpio_request(MOTOR_P_PIN, "dc motor +");
	gpio_request(MOTOR_M_PIN, "dc motor -");
	gpio_request(MOTOR_PWM_PIN, "dc motor speed");
	_motor_dc_ctrl(MOTOR_STANDBY);
	if (status < 0)
	{
		printk("gpio request Failed\n");
		goto exit_unregister;
	}
	//INIT_WORK(&motor_dc_work, motor_dc_handler);
	
	return 0;
exit_unregister:
	platform_device_unregister( pmotor_dc_dev);
	platform_driver_unregister(&motor_dc_driver);
exit:
	return -1;

}

static void motor_dc_exit(void)
{
	_motor_dc_ctrl(MOTOR_STANDBY);
	//gpio_free(MOTOR_P_PIN);
	//gpio_free(MOTOR_M_PIN);
	//gpio_free(MOTOR_PWM_PIN);
	//cancel_work_sync(&motor_dc_work);
	platform_device_unregister( pmotor_dc_dev);
	platform_driver_unregister(&motor_dc_driver);
	printk(" GoodBye, %s\n",MOTOR_NAME);
}



module_init( motor_dc_init);
module_exit( motor_dc_exit);

MODULE_AUTHOR("Eric Hsiao, erichsiao815@gmail.com");
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("dc motor control");


