/**
 * Sample driver for tact switch
 * File name: tactsw.c
 * Target board: Armadillo 440
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/fs.h>
#include <linux/gpio.h>
#include <linux/string.h>
#include <asm/uaccess.h>
#include <linux/cdev.h>

#define N_TACTSW	1	// number of minor devices
#define MSGLEN		256	// buffer length

static int tactsw_buttons[] = {	// board dependent parameters
  GPIO(3, 30),	// SW1
#if defined(CONFIG_MACH_ARMADILLO440)
  GPIO(2, 20),	// LCD_SW1
  GPIO(2, 29),	// LCD_SW2
  GPIO(2, 30),	// LCD_SW3
#if defined(CONFIG_ARMADILLO400_GPIO_A_B_KEY)
  GPIO(1, 0),	// SW2
  GPIO(1, 1),	// SW3
#endif /* CONFIG_ARMADILLO400_GPIO_A_B_KEY */
#endif /* CONFIG_MACH_ARMADILLO440 */
};
// character device
static struct cdev tactsw_dev;

// Info for the driver
static struct {
  int major;			// major number
  int nbuttons;			// number of tact switchs
  int *buttons;			// hardware parameters
  int used;			// true when used by a process,
  				// this flag inhibits open twice.
  int mlen;			// buffer filll count
  char msg[MSGLEN];		// buffer
  wait_queue_head_t wq;		// queue of procs waiting new input
  spinlock_t slock;		// for spin lock
} tactsw_info;

int sum=0;
struct pid *my_pid;

static int tactsw_open(struct inode *inode, struct file *filp)
{
  unsigned long irqflags;
  int retval = -EBUSY;
  spin_lock_irqsave(&(tactsw_info.slock), irqflags);
  if (tactsw_info.used == 0) {
    tactsw_info.used = 1;
    retval = 0;
  }
  spin_unlock_irqrestore(&(tactsw_info.slock), irqflags);
  return retval;
}

static int tactsw_read(struct file *filp, char *buff,
			size_t count, loff_t *pos)
{
  char *p1, *p2;
  size_t read_size;
  int i, ret;
  unsigned long irqflags;

  if (count <= 0) return -EFAULT;
 
  printk("mlen_line_75 = %d\n",tactsw_info.mlen);
  ret = wait_event_interruptible(tactsw_info.wq, (tactsw_info.mlen != 0) );
  
  if (ret != 0) return -EINTR;		// interrupted

  printk("mlen_line_80 = %d\n",tactsw_info.mlen);
  read_size = tactsw_info.mlen;		// atomic, so needless to spin lock
  if (count < read_size) read_size = count;
  
  if (copy_to_user(buff, tactsw_info.msg, read_size)) {
 
      printk("tactsw: copy_to_user error\n");
      // spin_unlock_irqrestore()
      return -EFAULT;
  }
  
  // Ring buffer is better.  But we prefer simplicity.
  p1 = tactsw_info.msg;
  p2 = p1+read_size;

  printk("read: spin_lock :mlen = %d\n",tactsw_info.mlen);
  spin_lock_irqsave(&(tactsw_info.slock), irqflags);
  // This subtraction is safe, since there is a single reader.
  printk("mlen_line_97 = %d\n",tactsw_info.mlen);
  tactsw_info.mlen -= read_size;
  for (i=tactsw_info.mlen; i>0; i--) *p1++=*p2++;
  spin_unlock_irqrestore(&(tactsw_info.slock), irqflags);
  printk("read: spin_unlock :mlen = %d\n",tactsw_info.mlen);

  return read_size;
}

/*
 シグナル通信を行うにあたってプロセス番号を利用するので、
 * 関数tactsw_ioctlでは引数のコマンドを用いてプロセス番号からプロセス本体のデータを得る。
 * case 2の使い始めとcase 3の使い終わりの関数を呼ぶことで、使わなくなったプロセス本体のデータを
 * 自動的に捨てることが可能となる。
 */
int tactsw_ioctl(struct inode *inode, struct file *filp,
		 unsigned int cmd, unsigned long arg)
{
	int retval=0;

	switch(cmd){
		case 1:
                    //fetch current status
		case 2:
			printk("case 2");
			my_pid = get_pid(task_pid(current)); //使い始め set signal
		case 3:
			printk("case 3");
			put_pid(my_pid); //使い終わりclear signal
		default:
		retval = -EFAULT;	// other code may be better
	}
	return retval;
}

static int tactsw_release(struct inode *inode, struct file *filp)
{
  printk("inode = %d, file = %d\n",*inode,*filp);
  tactsw_info.used = 0;
  return 0;
}

static irqreturn_t tactsw_intr(int irq, void *dev_id)
{
  int i;
  
  for (i = 0; i < tactsw_info.nbuttons; i++) {
    int gpio = tactsw_info.buttons[i];
    int prev_sum;
    if (irq == gpio_to_irq(gpio)) {
      int mlen;
      unsigned long irqflags;
      int val = gpio_get_value(gpio);
      int ch;
	int x;
      if (val==0) ch='1'; else ch='0';		// val=0 when key is pushed
	
      spin_lock_irqsave(&(tactsw_info.slock), irqflags);
      mlen = tactsw_info.mlen;
/*
 * スイッチが押されたり離されたりすると関数tactsw_int内でmlenの値が増加するため配列msgに情報が格納される。
 * 故にmsgの中身を見ることで押されている状態なのか離されている状態なのか分かる。
 */
      if (mlen < MSGLEN) { //mlen
	tactsw_info.msg[mlen] = ch; 
	
	tactsw_info.mlen = mlen+1;

	if(ch=='1'){  //SWITCH ON

            /*
             gpioをprintfしてみると、必ず左側のスイッチが62、真ん中のスイッチが61、右側のスイッチが52と出力されていることが分かった。
             * 従ってスイッチが押されたときにそのスイッチに応じて場合分けをし、
             * 右のスイッチから順にグローバル変数sumに+1、+2、+4をする機能を持たせた。
             * switch文を用いてsumの値で振り分け、sumを出力することでどのスイッチが押されたのか分かるようになっている。
             * ex) sum = 1 →　右のスイッチだけ押された
             * 　　sum = 2 →　真ん中のスイッチだけ押された
             *     sum = 3 →　右と真ん中のスイッチが同時に押された
             *     sum = 7 →　すべてのスイッチが同時に押された
             */
		if(gpio==52){ //right button
			sum=sum+1;
		}
		if(gpio==61){ //middle button
			sum=sum+2;
		}
		if(gpio==62){  //left button
			sum=sum+4;
		}
                /*
                 このswitch文内でタイマー機能を持たせる。3つのタクトスイッチの中で
                 * 右側のスイッチを押す→KillをしてシグナルSIGUSR1を送る
                 * 真ん中のボタンを押す→KillをしてシグナルSIGUSR2を送る
                 * という機能を持たせた。
                 */
		switch(sum){ 
			case 1:
				printk("START TIMER >>>> only right button is pressed\n",sum);
				kill_pid(my_pid,SIGUSR1,1);
				break;
			case 2:
				printk("STOP TIMER >>>> only middle button is pressed\n",sum);
				kill_pid(my_pid,SIGUSR2,1);
				break;
			case 4:
				printk("only left button is pressed\n",sum);
				break;
			case 3:
				printk("both right & middle buttons are pressed\n");
				break;
			case 5:
				printk("both right & left buttons are pressed\n");
				break;
			case 6:
				printk("both middle & left buttons are pressed\n");
				break;
			case 7:
				printk("all button is pressed\n");
				break;
			default:
				break;
		}		
		printk("   button_sum = %d\n",sum);
	}

        //スイッチが離されたときにsum=0をすることでスイッチを押すたびにどのスイッチが押されているのか判断することが可能
	if(ch=='0'){ 
		sum = 0;
	}
	wake_up_interruptible(&(tactsw_info.wq));
      }
      spin_unlock_irqrestore(&(tactsw_info.slock), irqflags); 

      return IRQ_HANDLED;
    }
  }
  return IRQ_NONE;
}

static struct file_operations tactsw_fops = {
  .read = tactsw_read,
  .ioctl = tactsw_ioctl,
  .open = tactsw_open,
  .release = tactsw_release,
};

static int __init tactsw_setup(int major)
{
  int i, error, gpio, irq;

  tactsw_info.major = major;
  tactsw_info.nbuttons = sizeof(tactsw_buttons)/sizeof(int);
  tactsw_info.buttons = tactsw_buttons;
  tactsw_info.used = 0;
  tactsw_info.mlen = 0;
  init_waitqueue_head(&(tactsw_info.wq));
  spin_lock_init(&(tactsw_info.slock));

  for (i = 0; i < tactsw_info.nbuttons; i++) {
    gpio = tactsw_info.buttons[i];

    error = gpio_request(gpio, "tactsw");
    // 2nd arg (label) is used for debug message and sysfs.
    if (error < 0) {
      printk("tactsw: gpio_request error %d (GPIO=%d)\n", error, gpio);
      goto fail;
    }

    error = gpio_direction_input(gpio);
    if (error < 0) {
      printk("tactsw: gpio_direction_input error %d (GPIO=%d)\n", error, gpio);
      goto free_fail;
    }

    irq = gpio_to_irq(gpio);
    if (irq < 0) {
      error = irq;
      printk("tactsw: gpio_to_irq error %d (GPIO=%d)\n", error, gpio);
      goto free_fail;
    }

    error = request_irq(irq, tactsw_intr,
			IRQF_TRIGGER_RISING | IRQF_TRIGGER_FALLING,
			"tactsw",	// used for debug message
			tactsw_intr);	// passed to isr's 2nd arg
    if (error) {
      printk("tactsw: request_irq error %d (IRQ=%d)\n", error, irq);
      goto free_fail;
    }
  }	// end of for

  return 0;
  
 free_fail:
  gpio_free(gpio);

 fail:
  while (--i >= 0) {
    gpio = tactsw_info.buttons[i];
    free_irq(gpio_to_irq(gpio), tactsw_intr);
    gpio_free(gpio);
  }
  return error;
}

static int __init tactsw_init(void)
{
  int ret, major;
  dev_t dev = MKDEV(0, 0);	

  ret = alloc_chrdev_region(&dev, 0, N_TACTSW, "tactsw");
  if (ret < 0) {
	return -1;
  }
  major = MAJOR(dev);
  printk("tactsw: Major number = %d.\n", major);

  cdev_init(&tactsw_dev, &tactsw_fops);
  tactsw_dev.owner = THIS_MODULE;

  ret = cdev_add(&tactsw_dev, MKDEV(major, 0), N_TACTSW);
  if (ret < 0) {
	printk("tactsw: cdev_add error\n");
	unregister_chrdev_region(dev, N_TACTSW);
	return -1;
  }

  ret = tactsw_setup(major);
  if (ret < 0) {
    printk("tactsw: setup error\n");
    cdev_del(&tactsw_dev);
    unregister_chrdev_region(dev, N_TACTSW);
  }
  return ret;
}

static void __exit tactsw_exit(void)
{
  dev_t dev=MKDEV(tactsw_info.major, 0);
  int i;

  for (i = 0; i < tactsw_info.nbuttons; i++) {
    int gpio = tactsw_info.buttons[i];
    int irq = gpio_to_irq(gpio);
    free_irq(irq, tactsw_intr);
    gpio_free(gpio);
  }

  // delete devices
  cdev_del(&tactsw_dev);
  unregister_chrdev_region(dev, N_TACTSW);

  // wake up tasks
  // This case never occurs since OS rejects rmmod when the device is open.
  if (waitqueue_active(&(tactsw_info.wq))) {
    printk("tactsw: there remains waiting tasks.  waking up.\n");
    wake_up_all(&(tactsw_info.wq));
    // Strictly speaking, we have to wait all processes wake up.
  }
}

module_init(tactsw_init);
module_exit(tactsw_exit);


MODULE_AUTHOR("Project6");
MODULE_DESCRIPTION("tact switch driver for armadillo-440");
MODULE_LICENSE("GPL");

