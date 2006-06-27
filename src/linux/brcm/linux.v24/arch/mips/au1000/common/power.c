/*
 * BRIEF MODULE DESCRIPTION
 *	Au1000 Power Management routines.
 *
 * Copyright 2001 MontaVista Software Inc.
 * Author: MontaVista Software, Inc.
 *		ppopov@mvista.com or source@mvista.com
 *
 *  Some of the routines are right out of init/main.c, whose
 *  copyrights apply here.
 *
 *  This program is free software; you can redistribute	 it and/or modify it
 *  under  the terms of	 the GNU General  Public License as published by the
 *  Free Software Foundation;  either version 2 of the	License, or (at your
 *  option) any later version.
 *
 *  THIS  SOFTWARE  IS PROVIDED	  ``AS	IS'' AND   ANY	EXPRESS OR IMPLIED
 *  WARRANTIES,	  INCLUDING, BUT NOT  LIMITED  TO, THE IMPLIED WARRANTIES OF
 *  MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.  IN
 *  NO	EVENT  SHALL   THE AUTHOR  BE	 LIABLE FOR ANY	  DIRECT, INDIRECT,
 *  INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 *  NOT LIMITED	  TO, PROCUREMENT OF  SUBSTITUTE GOODS	OR SERVICES; LOSS OF
 *  USE, DATA,	OR PROFITS; OR	BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON
 *  ANY THEORY OF LIABILITY, WHETHER IN	 CONTRACT, STRICT LIABILITY, OR TORT
 *  (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 *  THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 *  You should have received a copy of the  GNU General Public License along
 *  with this program; if not, write  to the Free Software Foundation, Inc.,
 *  675 Mass Ave, Cambridge, MA 02139, USA.
 */

#include <linux/init.h>
#include <linux/pm.h>
#include <linux/slab.h>
#include <linux/sysctl.h>

#include <asm/string.h>
#include <asm/uaccess.h>
#include <asm/io.h>
#include <asm/system.h>
#include <asm/au1000.h>

#define DEBUG 1
#ifdef DEBUG
#  define DPRINTK(fmt, args...)	printk("%s: " fmt, __FUNCTION__ , ## args)
#else
#  define DPRINTK(fmt, args...)
#endif

static void calibrate_delay(void);

extern unsigned int get_au1x00_speed(void);
extern unsigned long get_au1x00_uart_baud_base(void);
extern void set_au1x00_uart_baud_base(unsigned long new_baud_base);
extern unsigned long save_local_and_disable(int controller);
extern void restore_local_and_enable(int controller, unsigned long mask);
extern void local_enable_irq(unsigned int irq_nr);

/* Quick acpi hack. This will have to change! */
#define	CTL_ACPI 9999
#define	ACPI_S1_SLP_TYP 19
#define	ACPI_SLEEP 21

#ifdef CONFIG_PM

static spinlock_t pm_lock = SPIN_LOCK_UNLOCKED;

/* We need to save/restore a bunch of core registers that are
 * either volatile or reset to some state across a processor sleep.
 * If reading a register doesn't provide a proper result for a
 * later restore, we have to provide a function for loading that
 * register and save a copy.
 *
 * We only have to save/restore registers that aren't otherwise
 * done as part of a driver pm_* function.
 */
static uint	sleep_aux_pll_cntrl;
static uint	sleep_cpu_pll_cntrl;
static uint	sleep_pin_function;
static uint	sleep_uart0_inten;
static uint	sleep_uart0_fifoctl;
static uint	sleep_uart0_linectl;
static uint	sleep_uart0_clkdiv;
static uint	sleep_uart0_enable;
static uint	sleep_usbhost_enable;
static uint	sleep_usbdev_enable;
static uint	sleep_static_memctlr[4][3];

/* Define this to cause the value you write to /proc/sys/pm/sleep to
 * set the TOY timer for the amount of time you want to sleep.
 * This is done mainly for testing, but may be useful in other cases.
 * The value is number of 32KHz ticks to sleep.
 */
#define SLEEP_TEST_TIMEOUT 1
#ifdef SLEEP_TEST_TIMEOUT
static	int	sleep_ticks;
void wakeup_counter0_set(int ticks);
#endif

static void
save_core_regs(void)
{
	extern void save_au1xxx_intctl(void);
	extern void pm_eth0_shutdown(void);

	/* Do the serial ports.....these really should be a pm_*
	 * registered function by the driver......but of course the
	 * standard serial driver doesn't understand our Au1xxx
	 * unique registers.
	 */
	sleep_uart0_inten = au_readl(UART0_ADDR + UART_IER);
	sleep_uart0_fifoctl = au_readl(UART0_ADDR + UART_FCR);
	sleep_uart0_linectl = au_readl(UART0_ADDR + UART_LCR);
	sleep_uart0_clkdiv = au_readl(UART0_ADDR + UART_CLK);
	sleep_uart0_enable = au_readl(UART0_ADDR + UART_MOD_CNTRL);

#ifndef CONFIG_SOC_AU1200
	/* Shutdown USB host/device.
	*/
	sleep_usbhost_enable = au_readl(USB_HOST_CONFIG);

	/* There appears to be some undocumented reset register....
	*/
	au_writel(0, 0xb0100004); au_sync();
	au_writel(0, USB_HOST_CONFIG); au_sync();

	sleep_usbdev_enable = au_readl(USBD_ENABLE);
	au_writel(0, USBD_ENABLE); au_sync();
#endif

	/* Save interrupt controller state.
	*/
	save_au1xxx_intctl();

	/* Clocks and PLLs.
	*/
	sleep_aux_pll_cntrl = au_readl(SYS_AUXPLL);

	/* We don't really need to do this one, but unless we
	 * write it again it won't have a valid value if we
	 * happen to read it.
	 */
	sleep_cpu_pll_cntrl = au_readl(SYS_CPUPLL);

	sleep_pin_function = au_readl(SYS_PINFUNC);

	/* Save the static memory controller configuration.
	*/
	sleep_static_memctlr[0][0] = au_readl(MEM_STCFG0);
	sleep_static_memctlr[0][1] = au_readl(MEM_STTIME0);
	sleep_static_memctlr[0][2] = au_readl(MEM_STADDR0);
	sleep_static_memctlr[1][0] = au_readl(MEM_STCFG1);
	sleep_static_memctlr[1][1] = au_readl(MEM_STTIME1);
	sleep_static_memctlr[1][2] = au_readl(MEM_STADDR1);
	sleep_static_memctlr[2][0] = au_readl(MEM_STCFG2);
	sleep_static_memctlr[2][1] = au_readl(MEM_STTIME2);
	sleep_static_memctlr[2][2] = au_readl(MEM_STADDR2);
	sleep_static_memctlr[3][0] = au_readl(MEM_STCFG3);
	sleep_static_memctlr[3][1] = au_readl(MEM_STTIME3);
	sleep_static_memctlr[3][2] = au_readl(MEM_STADDR3);
}

static void
restore_core_regs(void)
{
	extern void restore_au1xxx_intctl(void);
	extern void wakeup_counter0_adjust(void);

	au_writel(sleep_aux_pll_cntrl, SYS_AUXPLL); au_sync();
	au_writel(sleep_cpu_pll_cntrl, SYS_CPUPLL); au_sync();
	au_writel(sleep_pin_function, SYS_PINFUNC); au_sync();

	/* Restore the static memory controller configuration.
	*/
	au_writel(sleep_static_memctlr[0][0], MEM_STCFG0);
	au_writel(sleep_static_memctlr[0][1], MEM_STTIME0);
	au_writel(sleep_static_memctlr[0][2], MEM_STADDR0);
	au_writel(sleep_static_memctlr[1][0], MEM_STCFG1);
	au_writel(sleep_static_memctlr[1][1], MEM_STTIME1);
	au_writel(sleep_static_memctlr[1][2], MEM_STADDR1);
	au_writel(sleep_static_memctlr[2][0], MEM_STCFG2);
	au_writel(sleep_static_memctlr[2][1], MEM_STTIME2);
	au_writel(sleep_static_memctlr[2][2], MEM_STADDR2);
	au_writel(sleep_static_memctlr[3][0], MEM_STCFG3);
	au_writel(sleep_static_memctlr[3][1], MEM_STTIME3);
	au_writel(sleep_static_memctlr[3][2], MEM_STADDR3);

	/* Enable the UART if it was enabled before sleep.
	 * I guess I should define module control bits........
	 */
	if (sleep_uart0_enable & 0x02) {
		au_writel(0, UART0_ADDR + UART_MOD_CNTRL); au_sync();
		au_writel(1, UART0_ADDR + UART_MOD_CNTRL); au_sync();
		au_writel(3, UART0_ADDR + UART_MOD_CNTRL); au_sync();
		au_writel(sleep_uart0_inten, UART0_ADDR + UART_IER); au_sync();
		au_writel(sleep_uart0_fifoctl, UART0_ADDR + UART_FCR); au_sync();
		au_writel(sleep_uart0_linectl, UART0_ADDR + UART_LCR); au_sync();
		au_writel(sleep_uart0_clkdiv, UART0_ADDR + UART_CLK); au_sync();
	}

	restore_au1xxx_intctl();
	wakeup_counter0_adjust();
}

unsigned long suspend_mode;

void wakeup_from_suspend(void)
{
	suspend_mode = 0;
}

int au_sleep(void)
{
	unsigned long wakeup, flags;
	extern unsigned int save_and_sleep(void);

	spin_lock_irqsave(&pm_lock,flags);

	save_core_regs();

	/** The code below is all system dependent and we should probably
	 ** have a function call out of here to set this up.  You need
	 ** to configure the GPIO or timer interrupts that will bring
	 ** you out of sleep.
	 ** For testing, the TOY counter wakeup is useful.
	 **/

#if 1
	au_writel(au_readl(SYS_PINSTATERD) & ~(1 << 11), SYS_PINSTATERD);

	/* gpio 6 can cause a wake up event */
	wakeup = au_readl(SYS_WAKEMSK);
	wakeup &= ~(1 << 8);	/* turn off match20 wakeup */
	wakeup = 1 << 5;	/* turn on gpio 6 wakeup   */
#else
	/* For testing, allow match20 to wake us up.  */
#ifdef SLEEP_TEST_TIMEOUT
	wakeup_counter0_set(sleep_ticks);
#endif
	wakeup = 1 << 8;	/* turn on match20 wakeup   */
	wakeup = 0;
#endif
	au_writel(0, SYS_WAKESRC);	/* clear cause */
	au_sync();
	au_writel(wakeup, SYS_WAKEMSK);
	au_sync();
	DPRINTK("Entering sleep!\n");
	save_and_sleep();

	/* after a wakeup, the cpu vectors back to 0x1fc00000 so
	 * it's up to the boot code to get us back here.
	 */
	restore_core_regs();
	spin_unlock_irqrestore(&pm_lock, flags);
	DPRINTK("Leaving sleep!\n");
	return 0;
}

static int pm_do_sleep(ctl_table * ctl, int write, struct file *file,
		       void *buffer, size_t * len)
{
	int retval = 0;
#ifdef SLEEP_TEST_TIMEOUT
#define TMPBUFLEN2 16
	char buf[TMPBUFLEN2], *p;
#endif

	if (!write) {
		*len = 0;
	} else {
#ifdef SLEEP_TEST_TIMEOUT
		if (*len > TMPBUFLEN2 - 1) {
			return -EFAULT;
		}
		if (copy_from_user(buf, buffer, *len)) {
			return -EFAULT;
		}
		buf[*len] = 0;
		p = buf;
		sleep_ticks = simple_strtoul(p, &p, 0);
#endif
		retval = pm_send_all(PM_SUSPEND, (void *) 2);

		if (retval)
			return retval;
		au_sleep();
		retval = pm_send_all(PM_RESUME, (void *) 0);
	}
	return retval;
}

static int pm_do_suspend(ctl_table * ctl, int write, struct file *file,
			 void *buffer, size_t * len)
{
	int retval = 0;

	if (!write) {
		*len = 0;
	} else {
		retval = pm_send_all(PM_SUSPEND, (void *) 2);
		if (retval)
			return retval;
		suspend_mode = 1;

		retval = pm_send_all(PM_RESUME, (void *) 0);
	}
	return retval;
}


static struct ctl_table pm_table[] = {
	{ACPI_S1_SLP_TYP, "suspend", NULL, 0, 0600, NULL, &pm_do_suspend},
	{ACPI_SLEEP, "sleep", NULL, 0, 0600, NULL, &pm_do_sleep},
	{0}
};

static struct ctl_table pm_dir_table[] = {
	{CTL_ACPI, "pm", NULL, 0, 0555, pm_table},
	{0}
};

/*
 * Initialize power interface
 */
static int __init pm_init(void)
{
	register_sysctl_table(pm_dir_table, 1);
	return 0;
}

__initcall(pm_init);


/*
 * This is right out of init/main.c
 */

/* This is the number of bits of precision for the loops_per_jiffy.  Each
   bit takes on average 1.5/HZ seconds.  This (like the original) is a little
   better than 1% */
#define LPS_PREC 8

static void calibrate_delay(void)
{
	unsigned long ticks, loopbit;
	int lps_precision = LPS_PREC;

	loops_per_jiffy = (1 << 12);

	while (loops_per_jiffy <<= 1) {
		/* wait for "start of" clock tick */
		ticks = jiffies;
		while (ticks == jiffies)
			/* nothing */ ;
		/* Go .. */
		ticks = jiffies;
		__delay(loops_per_jiffy);
		ticks = jiffies - ticks;
		if (ticks)
			break;
	}

/* Do a binary approximation to get loops_per_jiffy set to equal one clock
   (up to lps_precision bits) */
	loops_per_jiffy >>= 1;
	loopbit = loops_per_jiffy;
	while (lps_precision-- && (loopbit >>= 1)) {
		loops_per_jiffy |= loopbit;
		ticks = jiffies;
		while (ticks == jiffies);
		ticks = jiffies;
		__delay(loops_per_jiffy);
		if (jiffies != ticks)	/* longer than 1 tick */
			loops_per_jiffy &= ~loopbit;
	}
}
#endif				/* CONFIG_PM */
