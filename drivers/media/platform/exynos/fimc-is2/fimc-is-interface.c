/*
 * drivers/media/video/exynos/fimc-is-mc2/fimc-is-interface.c
 *
 * Copyright (c) 2011 Samsung Electronics Co., Ltd.
 *		http://www.samsung.com
 *
 * The header file related to camera
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/workqueue.h>
#include <linux/bug.h>

#include "fimc-is-core.h"
#include "fimc-is-regs.h"
#include "fimc-is-err.h"
#include "fimc-is-groupmgr.h"
#include "fimc-is-subdev-ctrl.h"

#include "fimc-is-interface.h"
#include "fimc-is-debug.h"
#include "fimc-is-clk-gate.h"

u32 __iomem *notify_fcount_sen0;
u32 __iomem *notify_fcount_sen1;
u32 __iomem *notify_fcount_sen2;
u32 __iomem *last_fcount0;
u32 __iomem *last_fcount1;

#define RESUME_BIT		(0x0)
#define SUSPEND_BIT		(0x2)
#define SENSOR_INIT		(0x4)
#define LCD_ON			(0x6)

#define init_request_barrier(itf) mutex_init(&itf->request_barrier)
#define enter_request_barrier(itf) mutex_lock(&itf->request_barrier);
#define exit_request_barrier(itf) mutex_unlock(&itf->request_barrier);
#define init_process_barrier(itf) spin_lock_init(&itf->process_barrier);
#define enter_process_barrier(itf) spin_lock_irq(&itf->process_barrier);
#define exit_process_barrier(itf) spin_unlock_irq(&itf->process_barrier);

extern struct fimc_is_sysfs_debug sysfs_debug;

/* func to register error report callback */
int fimc_is_set_err_report_vendor(struct fimc_is_interface *itf,
		void *err_report_data,
		int (*err_report_vendor)(void *data, u32 err_report_type))
{
	if (itf) {
		itf->err_report_data = err_report_data;
		itf->err_report_vendor = err_report_vendor;
	}

	return 0;
}

/* main func to handle error report */
static int fimc_is_err_report_handler(struct fimc_is_interface *itf, struct fimc_is_msg *msg)
{
	struct fimc_is_device_ischain *device;
	struct fimc_is_core *core;
	unsigned long dtp_wait_time;

	core = itf->core;
	device = &core->ischain[msg->instance];
	dtp_wait_time = device->sensor->dtp_timer.expires;

	err("IHC_REPORT_ERR(%d,%d,%d,%d,%d) is occured",
			msg->instance,
			msg->group,
			msg->param1,
			msg->param2,
			msg->param3);

	/*
	 * if vendor error report func was registered,
	 * call the func.
	 */
	if (itf->err_report_vendor)
		itf->err_report_vendor(itf->err_report_data, msg->param2);

	switch (msg->param2) {
	case REPORT_ERR_CIS_ID:
		warn("Occured the ERR_ID");
		break;
	case REPORT_ERR_CIS_ECC:
		warn("Occured the ERR_ECC");
	case REPORT_ERR_CIS_CRC:
		warn("Occured the ERR_CRC");
#ifdef ENABLE_DTP
		if (dtp_wait_time >= jiffies)
			set_bit(FIMC_IS_BAD_FRAME_STOP, &device->sensor->force_stop);
#endif
		break;
	case REPORT_ERR_CIS_OVERFLOW_VC0:
		warn("Occured the OVERFLOW_VC0");
		break;
	case REPORT_ERR_CIS_LOST_FE_VC0:
		warn("Occured the LOST_FE_VC0");
		break;
	case REPORT_ERR_CIS_LOST_FS_VC0:
		warn("Occured the LOST_FS_VC0");
		break;
	case REPORT_ERR_CIS_SOT_VC0:
		warn("Occured the SOT_VC0");
		break;
	case REPORT_ERR_CIS_SOT_VC1:
		warn("Occured the SOT_VC1");
		break;
	case REPORT_ERR_CIS_SOT_VC2:
		warn("Occured the SOT_VC2");
		break;
	case REPORT_ERR_CIS_SOT_VC3:
		warn("Occured the SOT_VC3");
		break;
	case REPORT_ERR_3AA_OVERFLOW:
		warn("Occured the 3AA_OVERFLOW");
		break;
	case REPORT_ERR_FLITE_D_OVERFLOW:
		warn("Occured the FLITE_D_OVERFLOW");
		break;
	case REPORT_ERR_COMPANION_LOST_FRAME:
		warn("Occured the COMPANION_LOST_FRAME");
		break;
	case REPORT_ERR_3A_ALGORITHM_DELAYED:
		warn("Occured the 3A_ALGORITHM_DELAYED");
		break;
	default:
		warn("parameter is default");
		break;
	}
	return 0;
}

void fimc_is_storefirm(struct fimc_is_interface *this)
{
	struct fimc_is_core *core = this->core;
	size_t fw_size = FIMC_IS_A5_MEM_SIZE;

	info("%s\n", __func__);

	vb2_ion_sync_for_cpu(core->resourcemgr.minfo.fw_cookie,
		0,
		FIMC_IS_BACKUP_SIZE,
		DMA_BIDIRECTIONAL);

	memcpy((void *)(this->itf_kvaddr + fw_size),
			(void *)(this->itf_kvaddr), FIMC_IS_BACKUP_SIZE);
}

void fimc_is_restorefirm(struct fimc_is_interface *this)
{
	struct fimc_is_core *core = this->core;
	size_t fw_size = FIMC_IS_A5_MEM_SIZE;

	info("%s\n", __func__);

	memcpy((void *)(this->itf_kvaddr),
		(void *)(this->itf_kvaddr + fw_size), FIMC_IS_BACKUP_SIZE);

	vb2_ion_sync_for_device(core->resourcemgr.minfo.fw_cookie,
		0,
		FIMC_IS_BACKUP_SIZE,
		DMA_BIDIRECTIONAL);
}

int fimc_is_set_fwboot(struct fimc_is_interface *this, int val) {
	int ret = 0;
	u32 set = 0;

	BUG_ON(!this);

#ifdef FW_SUSPEND_RESUME
	switch (val) {
	case FIRST_LAUNCHING:
		/* first launching */
		set &= ~(1 << RESUME_BIT);
		set |= (1 << SUSPEND_BIT);
		clear_bit(IS_IF_RESUME, &this->fw_boot);
		set_bit(IS_IF_SUSPEND, &this->fw_boot);
		break;
	case WARM_BOOT:
		/* TWIZ & main camera*/
		set |= (1 << RESUME_BIT) | (1 << SUSPEND_BIT);
		set_bit(IS_IF_RESUME, &this->fw_boot);
		set_bit(IS_IF_SUSPEND, &this->fw_boot);
		break;
	case COLD_BOOT:
		/* front camera | !TWIZ camera | dual camera */
		set = 0;
		clear_bit(IS_IF_RESUME, &this->fw_boot);
		clear_bit(IS_IF_SUSPEND, &this->fw_boot);
	break;
	default:
		err("unsupported val(0x%X)", val);
		set = 0;
		clear_bit(IS_IF_RESUME, &this->fw_boot);
		clear_bit(IS_IF_SUSPEND, &this->fw_boot);
		ret = -EINVAL;
		break;
	}
#endif

	this->com_regs->set_fwboot = set;
	info(" fwboot : 0x%d\n", this->com_regs->set_fwboot);

	return ret;
}

int print_fre_work_list(struct fimc_is_work_list *this)
{
	struct fimc_is_work *work, *temp;

	if (!(this->id & TRACE_WORK_ID_MASK))
		return 0;

	printk(KERN_ERR "[INF] fre(%02X, %02d) :", this->id, this->work_free_cnt);

	list_for_each_entry_safe(work, temp, &this->work_free_head, list) {
		printk(KERN_CONT "%X(%d)->", work->msg.command, work->fcount);
	}

	printk(KERN_CONT "X\n");

	return 0;
}

static int set_free_work(struct fimc_is_work_list *this,
	struct fimc_is_work *work)
{
	int ret = 0;
	unsigned long flags;

	if (work) {
		spin_lock_irqsave(&this->slock_free, flags);

		list_add_tail(&work->list, &this->work_free_head);
		this->work_free_cnt++;
#ifdef TRACE_WORK
		print_fre_work_list(this);
#endif

		spin_unlock_irqrestore(&this->slock_free, flags);
	} else {
		ret = -EFAULT;
		err("item is null ptr\n");
	}

	return ret;
}

static int get_free_work(struct fimc_is_work_list *this,
	struct fimc_is_work **work)
{
	int ret = 0;
	unsigned long flags;

	if (work) {
		spin_lock_irqsave(&this->slock_free, flags);

		if (this->work_free_cnt) {
			*work = container_of(this->work_free_head.next,
					struct fimc_is_work, list);
			list_del(&(*work)->list);
			this->work_free_cnt--;
		} else
			*work = NULL;

		spin_unlock_irqrestore(&this->slock_free, flags);
	} else {
		ret = -EFAULT;
		err("item is null ptr");
	}

	return ret;
}

static int get_free_work_irq(struct fimc_is_work_list *this,
	struct fimc_is_work **work)
{
	int ret = 0;

	if (work) {
		spin_lock(&this->slock_free);

		if (this->work_free_cnt) {
			*work = container_of(this->work_free_head.next,
					struct fimc_is_work, list);
			list_del(&(*work)->list);
			this->work_free_cnt--;
		} else
			*work = NULL;

		spin_unlock(&this->slock_free);
	} else {
		ret = -EFAULT;
		err("item is null ptr");
	}

	return ret;
}

int print_req_work_list(struct fimc_is_work_list *this)
{
	struct fimc_is_work *work, *temp;

	if (!(this->id & TRACE_WORK_ID_MASK))
		return 0;

	printk(KERN_ERR "[INF] req(%02X, %02d) :", this->id, this->work_request_cnt);

	list_for_each_entry_safe(work, temp, &this->work_request_head, list) {
		printk(KERN_CONT "%X(%d)->", work->msg.command, work->fcount);
	}

	printk(KERN_CONT "X\n");

	return 0;
}

static int print_work_data_state(struct fimc_is_interface *this)
{
	struct work_struct *work;
	unsigned long *bits = NULL;

	work = &this->work_wq[INTR_GENERAL];
	bits = (work_data_bits(work));
	info("INTR_GENERAL wq state : (0x%lx)", *bits);
	work = &this->work_wq[INTR_30C_FDONE];
	bits = (work_data_bits(work));
	info("INTR_30C_FDONE wq state : (0x%lx)", *bits);
	work = &this->work_wq[INTR_31C_FDONE];
	bits = (work_data_bits(work));
	info("INTR_31C_FDONE wq state : (0x%lx)", *bits);
	work = &this->work_wq[INTR_30P_FDONE];
	bits = (work_data_bits(work));
	info("INTR_30P_FDONE wq state : (0x%lx)", *bits);
	work = &this->work_wq[INTR_31P_FDONE];
	bits = (work_data_bits(work));
	info("INTR_31P_FDONE wq state : (0x%lx)", *bits);
	work = &this->work_wq[INTR_I0X_FDONE];
	bits = (work_data_bits(work));
	info("INTR_I0X_FDONE wq state : (0x%lx)", *bits);
	work = &this->work_wq[INTR_I1X_FDONE];
	bits = (work_data_bits(work));
	info("INTR_I1X_FDONE wq state : (0x%lx)", *bits);
	work = &this->work_wq[INTR_SCX_FDONE];
	bits = (work_data_bits(work));
	info("INTR_SCC_FDONE wq state : (0x%lx)", *bits);
	work = &this->work_wq[INTR_SHOT_DONE];
	bits = (work_data_bits(work));
	info("INTR_SHOT_DONE wq state : (0x%lx)", *bits);

	return 0;
}

static int set_req_work(struct fimc_is_work_list *this,
	struct fimc_is_work *work)
{
	int ret = 0;
	unsigned long flags;

	if (work) {
		spin_lock_irqsave(&this->slock_request, flags);

		list_add_tail(&work->list, &this->work_request_head);
		this->work_request_cnt++;
#ifdef TRACE_WORK
		print_req_work_list(this);
#endif

		spin_unlock_irqrestore(&this->slock_request, flags);
	} else {
		ret = -EFAULT;
		err("item is null ptr\n");
	}

	return ret;
}

static int set_req_work_irq(struct fimc_is_work_list *this,
	struct fimc_is_work *work)
{
	int ret = 0;

	if (work) {
		spin_lock(&this->slock_request);

		list_add_tail(&work->list, &this->work_request_head);
		this->work_request_cnt++;
#ifdef TRACE_WORK
		print_req_work_list(this);
#endif

		spin_unlock(&this->slock_request);
	} else {
		ret = -EFAULT;
		err("item is null ptr\n");
	}

	return ret;
}

static int get_req_work(struct fimc_is_work_list *this,
	struct fimc_is_work **work)
{
	int ret = 0;
	unsigned long flags;

	if (work) {
		spin_lock_irqsave(&this->slock_request, flags);

		if (this->work_request_cnt) {
			*work = container_of(this->work_request_head.next,
					struct fimc_is_work, list);
			list_del(&(*work)->list);
			this->work_request_cnt--;
		} else
			*work = NULL;

		spin_unlock_irqrestore(&this->slock_request, flags);
	} else {
		ret = -EFAULT;
		err("item is null ptr\n");
	}

	return ret;
}

static void init_work_list(struct fimc_is_work_list *this, u32 id, u32 count)
{
	u32 i;

	this->id = id;
	this->work_free_cnt	= 0;
	this->work_request_cnt	= 0;
	INIT_LIST_HEAD(&this->work_free_head);
	INIT_LIST_HEAD(&this->work_request_head);
	spin_lock_init(&this->slock_free);
	spin_lock_init(&this->slock_request);
	for (i = 0; i < count; ++i)
		set_free_work(this, &this->work[i]);

	init_waitqueue_head(&this->wait_queue);
}

static int set_busystate(struct fimc_is_interface *this,
	u32 command)
{
	int ret;

	ret = test_and_set_bit(IS_IF_STATE_BUSY, &this->state);
	if (ret)
		warn("%d command : busy state is already set", command);

	return ret;
}

static int clr_busystate(struct fimc_is_interface *this,
	u32 command)
{
	int ret;

	ret = test_and_clear_bit(IS_IF_STATE_BUSY, &this->state);
	if (!ret)
		warn("%d command : busy state is already clr", command);

	return !ret;
}

static int test_busystate(struct fimc_is_interface *this)
{
	int ret = 0;

	ret = test_bit(IS_IF_STATE_BUSY, &this->state);

	return ret;
}

static int wait_lockstate(struct fimc_is_interface *this)
{
	int ret = 0;

	ret = wait_event_timeout(this->lock_wait_queue,
		!atomic_read(&this->lock_pid), FIMC_IS_COMMAND_TIMEOUT);
	if (ret) {
		ret = 0;
	} else {
		err("timeout");
		ret = -ETIME;
	}

	return ret;
}

static int wait_idlestate(struct fimc_is_interface *this)
{
	int ret = 0;

	ret = wait_event_timeout(this->idle_wait_queue,
		!test_busystate(this), FIMC_IS_COMMAND_TIMEOUT);
	if (ret) {
		ret = 0;
	} else {
		err("timeout");
		ret = -ETIME;
	}

	return ret;
}

static int wait_initstate(struct fimc_is_interface *this)
{
	int ret = 0;

	ret = wait_event_timeout(this->init_wait_queue,
		test_bit(IS_IF_STATE_START, &this->state),
		FIMC_IS_STARTUP_TIMEOUT);
	if (ret) {
		ret = 0;
	} else {
		err("timeout");
		ret = -ETIME;
	}

	return ret;
}

static void testnclr_wakeup(struct fimc_is_interface *this,
	u32 command)
{
	int ret = 0;

	ret = clr_busystate(this, command);
	if (ret)
		err("current state is invalid(%ld)", this->state);

	wake_up(&this->idle_wait_queue);
}

static int waiting_is_ready(struct fimc_is_interface *itf)
{
	int ret = 0;
	u32 try_count = TRY_RECV_AWARE_COUNT;
	u32 cfg = readl(itf->regs + INTMSR0);
	u32 status = INTMSR0_GET_INTMSD0(cfg);

	while (status) {
		cfg = readl(itf->regs + INTMSR0);
		status = INTMSR0_GET_INTMSD0(cfg);
		udelay(100);
		dbg_interface("Retry to read INTMSR0(%d)\n", try_count);

		if (--try_count == 0) {
			err("INTMSR0's 0 bit is not cleared.");
			fimc_is_hw_logdump(itf);
			ret = -EINVAL;
			break;
		}
	}

	return ret;
}

static void send_interrupt(struct fimc_is_interface *interface)
{
	writel(INTGR0_INTGD0, interface->regs + INTGR0);
}

static int fimc_is_set_cmd(struct fimc_is_interface *itf,
	struct fimc_is_msg *msg,
	struct fimc_is_msg *reply)
{
	int ret = 0;
	int wait_lock = 0;
	u32 lock_pid = 0;
	u32 id;
	volatile struct is_common_reg __iomem *com_regs;
#ifdef MEASURE_TIME
#ifdef INTERFACE_TIME
	struct timeval measure_str, measure_end;
#endif
#endif

	BUG_ON(!itf);
	BUG_ON(!msg);
	BUG_ON(msg->instance >= FIMC_IS_STREAM_COUNT);
	BUG_ON(msg->command >= HIC_COMMAND_END);
	BUG_ON(!reply);

	if (!test_bit(IS_IF_STATE_OPEN, &itf->state)) {
		warn("interface close, %d cmd is cancel", msg->command);
		goto exit;
	}

	lock_pid = atomic_read(&itf->lock_pid);
	if (lock_pid && (lock_pid != current->pid)) {
		pr_info("itf LOCK, %d(%d) wait\n", current->pid, msg->command);
		wait_lock = wait_lockstate(itf);
		pr_info("itf UNLOCK, %d(%d) go\n", current->pid, msg->command);
		if (wait_lock) {
			err("wait_lockstate is fail, lock reset");
			atomic_set(&itf->lock_pid, 0);
		}
	}

	enter_request_barrier(itf);
	itf->request_msg = *msg;

#ifdef MEASURE_TIME
#ifdef INTERFACE_TIME
	do_gettimeofday(&measure_str);
#endif
#endif

	switch (msg->command) {
	case HIC_STREAM_ON:
		if (itf->streaming[msg->instance] == IS_IF_STREAMING_ON)
			mwarn("already stream on", msg);
		break;
	case HIC_STREAM_OFF:
		if (itf->streaming[msg->instance] == IS_IF_STREAMING_OFF)
			mwarn("already stream off", msg);
		break;
	case HIC_PROCESS_START:
		if (itf->processing[msg->instance] == IS_IF_PROCESSING_ON)
			mwarn("already process start", msg);
		break;
	case HIC_PROCESS_STOP:
		if (itf->processing[msg->instance] == IS_IF_PROCESSING_OFF)
			mwarn("already process stop", msg);
		break;
	case HIC_POWER_DOWN:
		if (itf->pdown_ready == IS_IF_POWER_DOWN_READY)
			mwarn("already powerdown ready", msg);
		break;
	default:
		if (itf->pdown_ready == IS_IF_POWER_DOWN_READY) {
			exit_request_barrier(itf);
			mwarn("already powerdown ready, %d cmd is cancel", msg, msg->command);
			goto exit;
		}
		break;
	}

	enter_process_barrier(itf);
	itf->process_msg = *msg;

	ret = waiting_is_ready(itf);
	if (ret) {
		exit_request_barrier(itf);
		exit_process_barrier(itf);
		err("waiting for ready is fail");
		ret = -EBUSY;
		goto exit;
	}

	set_busystate(itf, msg->command);
	com_regs = itf->com_regs;
	id = (msg->group << GROUP_ID_SHIFT) | msg->instance;
	writel(msg->command, &com_regs->hicmd);
	writel(id, &com_regs->hic_stream);
	writel(msg->param1, &com_regs->hic_param1);
	writel(msg->param2, &com_regs->hic_param2);
	writel(msg->param3, &com_regs->hic_param3);
	writel(msg->param4, &com_regs->hic_param4);
	send_interrupt(itf);

	itf->process_msg.command = 0;
	exit_process_barrier(itf);

	ret = wait_idlestate(itf);
	if (ret) {
		exit_request_barrier(itf);
		err("%d command is timeout", msg->command);
		fimc_is_hw_regdump(itf);
		print_req_work_list(&itf->work_list[INTR_GENERAL]);
		print_work_data_state(itf);
		clr_busystate(itf, msg->command);
		ret = -ETIME;
		goto exit;
	}

	reply->command = itf->reply.command;
	reply->group = itf->reply.group;
	reply->instance = itf->reply.instance;
	reply->param1 = itf->reply.param1;
	reply->param2 = itf->reply.param2;
	reply->param3 = itf->reply.param3;
	reply->param4 = itf->reply.param4;

	if (reply->command == ISR_DONE) {
		if (msg->command != reply->param1) {
			exit_request_barrier(itf);
			err("invalid command reply(%d != %d)", msg->command, reply->param1);
			ret = -EINVAL;
			goto exit;
		}

		switch (msg->command) {
		case HIC_STREAM_ON:
			itf->streaming[msg->instance] = IS_IF_STREAMING_ON;
			break;
		case HIC_STREAM_OFF:
			itf->streaming[msg->instance] = IS_IF_STREAMING_OFF;
			break;
		case HIC_PROCESS_START:
			itf->processing[msg->instance] = IS_IF_PROCESSING_ON;
			break;
		case HIC_PROCESS_STOP:
			itf->processing[msg->instance] = IS_IF_PROCESSING_OFF;
			break;
		case HIC_POWER_DOWN:
			itf->pdown_ready = IS_IF_POWER_DOWN_READY;
			break;
		case HIC_OPEN_SENSOR:
			if (reply->param1 == HIC_POWER_DOWN) {
				err("firmware power down");
				itf->pdown_ready = IS_IF_POWER_DOWN_READY;
				ret = -ECANCELED;
			} else {
				itf->pdown_ready = IS_IF_POWER_DOWN_NREADY;
			}
			break;
		default:
			break;
		}
	} else {
		itf->first_launch = false;
		err("ISR_NDONE is occured");
		ret = -EINVAL;
	}

#ifdef MEASURE_TIME
#ifdef INTERFACE_TIME
	do_gettimeofday(&measure_end);
	measure_time(&itf->time[msg->command],
		msg->instance,
		msg->group,
		&measure_str,
		&measure_end);
#endif
#endif

	itf->request_msg.command = 0;
	exit_request_barrier(itf);

exit:
	if (ret)
		fimc_is_hw_logdump(itf);

	return ret;
}

static int fimc_is_set_cmd_shot(struct fimc_is_interface *itf,
	struct fimc_is_msg *msg)
{
	int ret = 0;
	u32 id;
	volatile struct is_common_reg __iomem *com_regs;
	ulong flags;

	BUG_ON(!itf);
	BUG_ON(!msg);
	BUG_ON(msg->instance >= FIMC_IS_STREAM_COUNT);

	if (!test_bit(IS_IF_STATE_OPEN, &itf->state)) {
		warn("interface close, %d cmd is cancel", msg->command);
		goto exit;
	}

	enter_process_barrier(itf);
	itf->process_msg = *msg;

	ret = waiting_is_ready(itf);
	if (ret) {
		exit_process_barrier(itf);
		err("waiting for ready is fail");
		ret = -EBUSY;
		goto exit;
	}

	spin_lock_irqsave(&itf->shot_check_lock, flags);
	atomic_set(&itf->shot_check[msg->instance], 1);
	spin_unlock_irqrestore(&itf->shot_check_lock, flags);

	com_regs = itf->com_regs;
	id = (msg->group << GROUP_ID_SHIFT) | msg->instance;
	writel(msg->command, &com_regs->hicmd);
	writel(id, &com_regs->hic_stream);
	writel(msg->param1, &com_regs->hic_param1);
	writel(msg->param2, &com_regs->hic_param2);
	writel(msg->param3, &com_regs->hic_param3);
	writel(msg->param4, &com_regs->hic_param4);
	send_interrupt(itf);

	itf->process_msg.command = 0;
	exit_process_barrier(itf);

exit:
	return ret;
}

static int fimc_is_set_cmd_nblk(struct fimc_is_interface *this,
	struct fimc_is_work *work)
{
	int ret = 0;
	u32 id;
	struct fimc_is_msg *msg;
	volatile struct is_common_reg __iomem *com_regs;

	msg = &work->msg;
	switch (msg->command) {
	case HIC_SET_CAM_CONTROL:
		set_req_work(&this->nblk_cam_ctrl, work);
		break;
	case HIC_MSG_TEST:
		break;
	default:
		err("unresolved command\n");
		break;
	}

	enter_process_barrier(this);

	ret = waiting_is_ready(this);
	if (ret) {
		err("waiting for ready is fail");
		ret = -EBUSY;
		goto exit;
	}

	com_regs = this->com_regs;
	id = (msg->group << GROUP_ID_SHIFT) | msg->instance;
	writel(msg->command, &com_regs->hicmd);
	writel(id, &com_regs->hic_stream);
	writel(msg->param1, &com_regs->hic_param1);
	writel(msg->param2, &com_regs->hic_param2);
	writel(msg->param3, &com_regs->hic_param3);
	writel(msg->param4, &com_regs->hic_param4);
	send_interrupt(this);

exit:
	exit_process_barrier(this);
	return ret;
}

static inline void fimc_is_get_cmd(struct fimc_is_interface *itf,
	struct fimc_is_msg *msg, u32 index)
{
	volatile struct is_common_reg __iomem *com_regs = itf->com_regs;

	switch (index) {
	case INTR_GENERAL:
		msg->id = 0;
		msg->command = readl(&com_regs->ihcmd);
		msg->instance = readl(&com_regs->ihc_stream);
		msg->param1 = readl(&com_regs->ihc_param1);
		msg->param2 = readl(&com_regs->ihc_param2);
		msg->param3 = readl(&com_regs->ihc_param3);
		msg->param4 = readl(&com_regs->ihc_param4);
		break;
	case INTR_SHOT_DONE:
		msg->id = 0;
		msg->command = IHC_FRAME_DONE;
		msg->instance = readl(&com_regs->shot_stream);
		msg->param1 = readl(&com_regs->shot_param1);
		msg->param2 = readl(&com_regs->shot_param2);
		msg->param3 = 0;
		msg->param4 = 0;
		break;
	case INTR_30C_FDONE:
		msg->id = 0;
		msg->command = IHC_FRAME_DONE;
		msg->instance = readl(&com_regs->t0c_stream);
		msg->param1 = readl(&com_regs->t0c_param1);
		msg->param2 = readl(&com_regs->t0c_param2);
		msg->param3 = (msg->param2 == 0xFFFFFFFF) ? 1 : 0;
		msg->param4 = 0;
		break;
	case INTR_30P_FDONE:
		msg->id = 0;
		msg->command = IHC_FRAME_DONE;
		msg->instance = readl(&com_regs->t0p_stream);
		msg->param1 = readl(&com_regs->t0p_param1);
		msg->param2 = readl(&com_regs->t0p_param2);
		msg->param3 = (msg->param2 == 0xFFFFFFFF) ? 1 : 0;
		msg->param4 = 0;
		break;
	case INTR_31C_FDONE:
		msg->id = 0;
		msg->command = IHC_FRAME_DONE;
		msg->instance = readl(&com_regs->t1c_stream);
		msg->param1 = readl(&com_regs->t1c_param1);
		msg->param2 = readl(&com_regs->t1c_param2);
		msg->param3 = (msg->param2 == 0xFFFFFFFF) ? 1 : 0;
		msg->param4 = 0;
		break;
	case INTR_31P_FDONE:
		msg->id = 0;
		msg->command = IHC_FRAME_DONE;
		msg->instance = readl(&com_regs->t1p_stream);
		msg->param1 = readl(&com_regs->t1p_param1);
		msg->param2 = readl(&com_regs->t1p_param2);
		msg->param3 = (msg->param2 == 0xFFFFFFFF) ? 1 : 0;
		msg->param4 = 0;
		break;
	case INTR_I0X_FDONE:
		msg->id = 0;
		msg->command = IHC_FRAME_DONE;
		msg->instance = readl(&com_regs->i0x_stream);
		msg->param1 = readl(&com_regs->i0x_param1);
		msg->param2 = readl(&com_regs->i0x_param2);
		msg->param3 = (msg->param2 == 0xFFFFFFFF) ? 1 : 0;
		msg->param4 = 0;
		break;
	case INTR_I1X_FDONE:
		msg->id = 0;
		msg->command = IHC_FRAME_DONE;
		msg->instance = readl(&com_regs->i1x_stream);
		msg->param1 = readl(&com_regs->i1x_param1);
		msg->param2 = readl(&com_regs->i1x_param2);
		msg->param3 = (msg->param2 == 0xFFFFFFFF) ? 1 : 0;
		msg->param4 = 0;
		break;
	case INTR_SCX_FDONE:
		msg->id = 0;
		msg->command = IHC_FRAME_DONE;
		msg->instance = readl(&com_regs->scx_stream);
		msg->param1 = readl(&com_regs->scx_param1);
		msg->param2 = readl(&com_regs->scx_param2);
		msg->param3 = (msg->param2 == 0xFFFFFFFF) ? 1 : 0;
		msg->param4 = 0;
		break;
	default:
		msg->id = 0;
		msg->command = 0;
		msg->instance = 0;
		msg->param1 = 0;
		msg->param2 = 0;
		msg->param3 = 0;
		msg->param4 = 0;
		err("unknown command getting\n");
		break;
	}

	msg->group = msg->instance >> GROUP_ID_SHIFT;
	msg->instance = msg->instance & GROUP_ID_MASK;
}

static inline u32 fimc_is_get_intr(struct fimc_is_interface *itf)
{
	return readl(itf->regs + INTMSR1);
}

static inline void fimc_is_clr_intr(struct fimc_is_interface *itf,
	u32 index)
{
	writel((1 << index), itf->regs + INTCR1);
}

static inline void wq_func_schedule(struct fimc_is_interface *itf,
	struct work_struct *work_wq)
{
	if (itf->workqueue)
		queue_work(itf->workqueue, work_wq);
	else
		schedule_work(work_wq);
}

static void wq_func_general(struct work_struct *data)
{
	struct fimc_is_interface *itf;
	struct fimc_is_msg *msg;
	struct fimc_is_work *work;
	struct fimc_is_work *nblk_work;

	itf = container_of(data, struct fimc_is_interface,
		work_wq[INTR_GENERAL]);

	get_req_work(&itf->work_list[INTR_GENERAL], &work);
	while (work) {
		msg = &work->msg;
		switch (msg->command) {
		case IHC_GET_SENSOR_NUMBER:
			info("IS Version: %d.%d [0x%02x]\n",
				ISDRV_VERSION, msg->param1,
				get_drv_clock_gate() |
				get_drv_dvfs());
			itf->pdown_ready = IS_IF_POWER_DOWN_NREADY;
			set_bit(IS_IF_STATE_START, &itf->state);
			wake_up(&itf->init_wait_queue);
			break;
		case ISR_DONE:
			switch (msg->param1) {
			case HIC_OPEN_SENSOR:
				dbg_interface("open done\n");
				memcpy(&itf->reply, msg,
					sizeof(struct fimc_is_msg));
				testnclr_wakeup(itf, msg->param1);
				break;
			case HIC_CLOSE_SENSOR:
				dbg_interface("close done\n");
				memcpy(&itf->reply, msg,
					sizeof(struct fimc_is_msg));
				testnclr_wakeup(itf, msg->param1);
				break;
			case HIC_GET_SET_FILE_ADDR:
				dbg_interface("saddr(%x) done\n",
					msg->param2);
				memcpy(&itf->reply, msg,
					sizeof(struct fimc_is_msg));
				testnclr_wakeup(itf, msg->param1);
				break;
			case HIC_LOAD_SET_FILE:
				dbg_interface("setfile done\n");
				memcpy(&itf->reply, msg,
					sizeof(struct fimc_is_msg));
				testnclr_wakeup(itf, msg->param1);
				break;
			case HIC_SET_A5_MAP:
				dbg_interface("mapping done\n");
				memcpy(&itf->reply, msg,
					sizeof(struct fimc_is_msg));
				testnclr_wakeup(itf, msg->param1);
				break;
			case HIC_SET_A5_UNMAP:
				dbg_interface("unmapping done\n");
				memcpy(&itf->reply, msg,
					sizeof(struct fimc_is_msg));
				testnclr_wakeup(itf, msg->param1);
				break;
			case HIC_PROCESS_START:
				dbg_interface("process_on done\n");
				memcpy(&itf->reply, msg,
					sizeof(struct fimc_is_msg));
				testnclr_wakeup(itf, msg->param1);
				break;
			case HIC_PROCESS_STOP:
				dbg_interface("process_off done\n");
				memcpy(&itf->reply, msg,
					sizeof(struct fimc_is_msg));
				testnclr_wakeup(itf, msg->param1);
				break;
			case HIC_STREAM_ON:
				dbg_interface("stream_on done\n");
				memcpy(&itf->reply, msg,
					sizeof(struct fimc_is_msg));
				testnclr_wakeup(itf, msg->param1);
				break;
			case HIC_STREAM_OFF:
				dbg_interface("stream_off done\n");
				memcpy(&itf->reply, msg,
					sizeof(struct fimc_is_msg));
				testnclr_wakeup(itf, msg->param1);
				break;
			case HIC_SET_PARAMETER:
				dbg_interface("s_param done\n");
				memcpy(&itf->reply, msg,
					sizeof(struct fimc_is_msg));
				testnclr_wakeup(itf, msg->param1);
				break;
			case HIC_GET_STATIC_METADATA:
				dbg_interface("g_capability done\n");
				memcpy(&itf->reply, msg,
					sizeof(struct fimc_is_msg));
				testnclr_wakeup(itf, msg->param1);
				break;
			case HIC_PREVIEW_STILL:
				dbg_interface("a_param(%dx%d) done\n",
					msg->param2,
					msg->param3);
				memcpy(&itf->reply, msg,
					sizeof(struct fimc_is_msg));
				testnclr_wakeup(itf, msg->param1);
				break;
			case HIC_POWER_DOWN:
				dbg_interface("powerdown done\n");
				memcpy(&itf->reply, msg,
					sizeof(struct fimc_is_msg));
				testnclr_wakeup(itf, msg->param1);
				break;
			case HIC_I2C_CONTROL_LOCK:
				dbg_interface("i2c lock done\n");
				memcpy(&itf->reply, msg,
					sizeof(struct fimc_is_msg));
				testnclr_wakeup(itf, msg->param1);
				break;
#if (FW_HAS_SYS_CTRL_CMD)
			case HIC_SYSTEM_CONTROL:
				dbg_interface("system control done\n");
				memcpy(&itf->reply, msg,
					sizeof(struct fimc_is_msg));
				testnclr_wakeup(itf, msg->param1);
				break;
#endif
#if (FW_HAS_SENSOR_MODE_CMD)
			case HIC_SENSOR_MODE_CHANGE:
				dbg_interface("sensor mode change done\n");
				memcpy(&itf->reply, msg,
					sizeof(struct fimc_is_msg));
				testnclr_wakeup(itf, msg->param1);
				break;
#endif
			/*non-blocking command*/
			case HIC_SHOT:
				err("shot done is not acceptable\n");
				break;
			case HIC_SET_CAM_CONTROL:
				/* this code will be used latter */
#if 0
				dbg_interface("camctrl done\n");
				get_req_work(&itf->nblk_cam_ctrl , &nblk_work);
				if (nblk_work) {
					nblk_work->msg.command = ISR_DONE;
					set_free_work(&itf->nblk_cam_ctrl,
						nblk_work);
				} else {
					err("nblk camctrl request is empty");
					print_fre_work_list(
						&itf->nblk_cam_ctrl);
					print_req_work_list(
						&itf->nblk_cam_ctrl);
				}
#else
				err("camctrl is not acceptable\n");
#endif
				break;
			default:
				err("unknown done is invokded\n");
				break;
			}
			break;
		case ISR_NDONE:
			fimc_is_hw_logdump(itf);
			switch (msg->param1) {
			case HIC_SHOT:
				err("[ITF:%d] shot NDONE is not acceptable",
					msg->instance);
				break;
			case HIC_SET_CAM_CONTROL:
				dbg_interface("camctrl NOT done\n");
				get_req_work(&itf->nblk_cam_ctrl , &nblk_work);
				nblk_work->msg.command = ISR_NDONE;
				set_free_work(&itf->nblk_cam_ctrl, nblk_work);
				break;
			case HIC_SET_PARAMETER:
				err("s_param NOT done");
				err("param2 : 0x%08X", msg->param2);
				err("param3 : 0x%08X", msg->param3);
				err("param4 : 0x%08X", msg->param4);
				memcpy(&itf->reply, msg,
					sizeof(struct fimc_is_msg));
				testnclr_wakeup(itf, msg->param1);
				break;
			default:
				err("a command(%d) not done", msg->param1);
				memcpy(&itf->reply, msg,
					sizeof(struct fimc_is_msg));
				testnclr_wakeup(itf, msg->param1);
				break;
			}
			break;
		case IHC_SET_FACE_MARK:
			err("FACE_MARK(%d,%d,%d) is not acceptable\n",
				msg->param1,
				msg->param2,
				msg->param3);
			break;
		case IHC_AA_DONE:
			err("AA_DONE(%d,%d,%d) is not acceptable\n",
				msg->param1,
				msg->param2,
				msg->param3);
			break;
		case IHC_FLASH_READY:
			err("IHC_FLASH_READY is not acceptable");
			break;
		case IHC_NOT_READY:
			err("IHC_NOT_READY is occured, need reset");
			fimc_is_hw_logdump(itf);
			break;
#if (FW_HAS_REPORT_ERR_CMD)
		case IHC_REPORT_ERR:
			err("IHC_REPORT_ERR is occured");
			fimc_is_err_report_handler(itf, msg);
			break;
#endif
		default:
			err("func_general unknown(0x%08X) end\n", msg->command);
			fimc_is_hw_logdump(itf);
			BUG();
			break;
		}

		set_free_work(&itf->work_list[INTR_GENERAL], work);
		get_req_work(&itf->work_list[INTR_GENERAL], &work);
	}
}

static void wq_func_subdev(struct fimc_is_subdev *leader,
	struct fimc_is_subdev *subdev,
	struct fimc_is_frame *sub_frame,
	u32 fcount, u32 rcount, u32 status)
{
	u32 done_state = VB2_BUF_STATE_DONE;
	u32 findex, mindex;
	struct fimc_is_video_ctx *ldr_vctx, *sub_vctx;
	struct fimc_is_framemgr *ldr_framemgr, *sub_framemgr;
	struct fimc_is_frame *ldr_frame;
	struct camera2_node *capture;

	BUG_ON(!sub_frame);

	ldr_vctx = leader->vctx;
	sub_vctx = subdev->vctx;

	ldr_framemgr = GET_FRAMEMGR(ldr_vctx);
	sub_framemgr = GET_FRAMEMGR(sub_vctx);

	findex = sub_frame->stream->findex;
	mindex = ldr_vctx->queue.buf_maxcount;
	if (findex >= mindex) {
		mserr("findex(%d) is invalid(max %d)", subdev, subdev, findex, mindex);
		done_state = VB2_BUF_STATE_ERROR;
		sub_frame->stream->fvalid = 0;
		goto complete;
	}

	ldr_frame = &ldr_framemgr->frame[findex];
	if (ldr_frame->fcount != fcount) {
		mserr("frame mismatched(ldr%d, sub%d)", subdev, subdev, ldr_frame->fcount, fcount);
		done_state = VB2_BUF_STATE_ERROR;
		sub_frame->stream->fvalid = 0;
		goto complete;
	}

	if (status) {
		msrinfo("[ERR] NDONE(%d, E%X)\n", subdev, subdev, ldr_frame, sub_frame->index, status);
		done_state = VB2_BUF_STATE_ERROR;
		sub_frame->stream->fvalid = 0;
	} else {
#ifdef DBG_STREAMING
		msrinfo(" DONE(%d)\n", subdev, subdev, ldr_frame, sub_frame->index);
#endif
		sub_frame->stream->fvalid = 1;
	}

	capture = &ldr_frame->shot_ext->node_group.capture[subdev->cid];
	if (likely(capture->vid == subdev->vid)) {
		sub_frame->stream->input_crop_region[0] = capture->input.cropRegion[0];
		sub_frame->stream->input_crop_region[1] = capture->input.cropRegion[1];
		sub_frame->stream->input_crop_region[2] = capture->input.cropRegion[2];
		sub_frame->stream->input_crop_region[3] = capture->input.cropRegion[3];
		sub_frame->stream->output_crop_region[0] = capture->output.cropRegion[0];
		sub_frame->stream->output_crop_region[1] = capture->output.cropRegion[1];
		sub_frame->stream->output_crop_region[2] = capture->output.cropRegion[2];
		sub_frame->stream->output_crop_region[3] = capture->output.cropRegion[3];
	} else {
		mserr("capture vid is changed(%d != %d)", subdev, subdev, subdev->vid, capture->vid);
		sub_frame->stream->input_crop_region[0] = 0;
		sub_frame->stream->input_crop_region[1] = 0;
		sub_frame->stream->input_crop_region[2] = 0;
		sub_frame->stream->input_crop_region[3] = 0;
		sub_frame->stream->output_crop_region[0] = 0;
		sub_frame->stream->output_crop_region[1] = 0;
		sub_frame->stream->output_crop_region[2] = 0;
		sub_frame->stream->output_crop_region[3] = 0;
	}

	clear_bit(subdev->id, &ldr_frame->out_flag);

complete:
	sub_frame->stream->fcount = fcount;
	sub_frame->stream->rcount = rcount;

	fimc_is_frame_trans_pro_to_com(sub_framemgr, sub_frame);
	buffer_done(sub_vctx, sub_frame->index, done_state);
}

static void wq_func_frame(struct fimc_is_subdev *leader,
	struct fimc_is_subdev *subdev,
	u32 fcount, u32 rcount, u32 status)
{
	unsigned long flags;
	struct fimc_is_framemgr *framemgr;
	struct fimc_is_frame *frame;

	BUG_ON(!leader);
	BUG_ON(!subdev);
	BUG_ON(!leader->vctx);
	BUG_ON(!subdev->vctx);
	BUG_ON(!leader->vctx->video);
	BUG_ON(!subdev->vctx->video);

	framemgr = GET_FRAMEMGR(subdev->vctx);

	framemgr_e_barrier_irqs(framemgr, FMGR_IDX_4, flags);

	fimc_is_frame_process_head(framemgr, &frame);
	if (frame) {
		if (!frame->stream) {
			mserr("stream is NULL", subdev, subdev);
			BUG();
		}

		if (unlikely(frame->stream->fcount != fcount)) {
			while (frame) {
				if (fcount == frame->stream->fcount) {
					wq_func_subdev(leader, subdev, frame, fcount, rcount, status);
					break;
				} else if (fcount > frame->stream->fcount) {
					wq_func_subdev(leader, subdev, frame, frame->stream->fcount, rcount, 0xF);

					/* get next subdev frame */
					fimc_is_frame_process_head(framemgr, &frame);
				} else {
					warn("%d frame done is ignored", frame->stream->fcount);
					break;
				}
			}
		} else {
			wq_func_subdev(leader, subdev, frame, fcount, rcount, status);
		}
	} else {
		mserr("frame done(%p) is occured without request", subdev, subdev, frame);
	}

	framemgr_x_barrier_irqr(framemgr, FMGR_IDX_4, flags);
}

static void wq_func_30c(struct work_struct *data)
{
	u32 instance, fcount, rcount, status;
	struct fimc_is_interface *itf;
	struct fimc_is_device_ischain *device;
	struct fimc_is_subdev *leader, *subdev;
	struct fimc_is_work *work;
	struct fimc_is_msg *msg;

	itf = container_of(data, struct fimc_is_interface, work_wq[WORK_30C_FDONE]);

	get_req_work(&itf->work_list[WORK_30C_FDONE], &work);
	while (work) {
		msg = &work->msg;
		instance = msg->instance;
		fcount = msg->param1;
		rcount = msg->param2;
		status = msg->param3;

		if (instance >= FIMC_IS_STREAM_COUNT) {
			err("instance is invalid(%d)", instance);
			goto p_err;
		}

		device = &((struct fimc_is_core *)itf->core)->ischain[instance];
		if (!device) {
			err("device is NULL");
			goto p_err;
		}

		if (!test_bit(FIMC_IS_ISCHAIN_OPEN, &device->state)) {
			merr("device is not open", device);
			goto p_err;
		}

		subdev = &device->txc;
		if (!test_bit(FIMC_IS_SUBDEV_START, &subdev->state)) {
			merr("subdev is not start", device);
			goto p_err;
		}

		leader = subdev->leader;
		if (!leader) {
			merr("leader is NULL", device);
			goto p_err;
		}

		wq_func_frame(leader, subdev, fcount, rcount, status);

p_err:
		set_free_work(&itf->work_list[WORK_30C_FDONE], work);
		get_req_work(&itf->work_list[WORK_30C_FDONE], &work);
	}
}

static void wq_func_30p(struct work_struct *data)
{
	u32 instance, fcount, rcount, status;
	struct fimc_is_interface *itf;
	struct fimc_is_device_ischain *device;
	struct fimc_is_subdev *leader, *subdev;
	struct fimc_is_work *work;
	struct fimc_is_msg *msg;

	itf = container_of(data, struct fimc_is_interface, work_wq[WORK_30P_FDONE]);

	get_req_work(&itf->work_list[WORK_30P_FDONE], &work);
	while (work) {
		msg = &work->msg;
		instance = msg->instance;
		fcount = msg->param1;
		rcount = msg->param2;
		status = msg->param3;

		if (instance >= FIMC_IS_STREAM_COUNT) {
			err("instance is invalid(%d)", instance);
			goto p_err;
		}

		device = &((struct fimc_is_core *)itf->core)->ischain[instance];
		if (!device) {
			err("device is NULL");
			goto p_err;
		}

		if (!test_bit(FIMC_IS_ISCHAIN_OPEN, &device->state)) {
			merr("device is not open", device);
			goto p_err;
		}

		subdev = &device->txp;
		if (!test_bit(FIMC_IS_SUBDEV_START, &subdev->state)) {
			merr("subdev is not start", device);
			goto p_err;
		}

		leader = subdev->leader;
		if (!leader) {
			merr("leader is NULL", device);
			goto p_err;
		}

		wq_func_frame(leader, subdev, fcount, rcount, status);

p_err:
		set_free_work(&itf->work_list[WORK_30P_FDONE], work);
		get_req_work(&itf->work_list[WORK_30P_FDONE], &work);
	}
}

static void wq_func_31c(struct work_struct *data)
{
	u32 instance, fcount, rcount, status;
	struct fimc_is_interface *itf;
	struct fimc_is_device_ischain *device;
	struct fimc_is_subdev *leader, *subdev;
	struct fimc_is_work *work;
	struct fimc_is_msg *msg;

	itf = container_of(data, struct fimc_is_interface, work_wq[WORK_31C_FDONE]);

	get_req_work(&itf->work_list[WORK_31C_FDONE], &work);
	while (work) {
		msg = &work->msg;
		instance = msg->instance;
		fcount = msg->param1;
		rcount = msg->param2;
		status = msg->param3;

		if (instance >= FIMC_IS_STREAM_COUNT) {
			err("instance is invalid(%d)", instance);
			goto p_err;
		}

		device = &((struct fimc_is_core *)itf->core)->ischain[instance];
		if (!device) {
			err("device is NULL");
			goto p_err;
		}

		if (!test_bit(FIMC_IS_ISCHAIN_OPEN, &device->state)) {
			merr("device is not open", device);
			goto p_err;
		}

		subdev = &device->txc;
		if (!test_bit(FIMC_IS_SUBDEV_START, &subdev->state)) {
			merr("subdev is not start", device);
			goto p_err;
		}

		leader = subdev->leader;
		if (!leader) {
			merr("leader is NULL", device);
			goto p_err;
		}

		wq_func_frame(leader, subdev, fcount, rcount, status);

p_err:
		set_free_work(&itf->work_list[WORK_31C_FDONE], work);
		get_req_work(&itf->work_list[WORK_31C_FDONE], &work);
	}
}

static void wq_func_31p(struct work_struct *data)
{
	u32 instance, fcount, rcount, status;
	struct fimc_is_interface *itf;
	struct fimc_is_device_ischain *device;
	struct fimc_is_subdev *leader, *subdev;
	struct fimc_is_work *work;
	struct fimc_is_msg *msg;

	itf = container_of(data, struct fimc_is_interface, work_wq[WORK_31P_FDONE]);

	get_req_work(&itf->work_list[WORK_31P_FDONE], &work);
	while (work) {
		msg = &work->msg;
		instance = msg->instance;
		fcount = msg->param1;
		rcount = msg->param2;
		status = msg->param3;

		if (instance >= FIMC_IS_STREAM_COUNT) {
			err("instance is invalid(%d)", instance);
			goto p_err;
		}

		device = &((struct fimc_is_core *)itf->core)->ischain[instance];
		if (!device) {
			err("device is NULL");
			goto p_err;
		}

		if (!test_bit(FIMC_IS_ISCHAIN_OPEN, &device->state)) {
			merr("device is not open", device);
			goto p_err;
		}

		subdev = &device->txp;
		if (!test_bit(FIMC_IS_SUBDEV_START, &subdev->state)) {
			merr("subdev is not start", device);
			goto p_err;
		}

		leader = subdev->leader;
		if (!leader) {
			merr("leader is NULL", device);
			goto p_err;
		}

		wq_func_frame(leader, subdev, fcount, rcount, status);

p_err:
		set_free_work(&itf->work_list[WORK_31P_FDONE], work);
		get_req_work(&itf->work_list[WORK_31P_FDONE], &work);
	}
}

static void wq_func_i0c(struct work_struct *data)
{
	u32 instance, fcount, rcount, status;
	struct fimc_is_interface *itf;
	struct fimc_is_device_ischain *device;
	struct fimc_is_subdev *leader, *subdev;
	struct fimc_is_work *work;
	struct fimc_is_msg *msg;

	itf = container_of(data, struct fimc_is_interface, work_wq[WORK_I0C_FDONE]);

	get_req_work(&itf->work_list[WORK_I0C_FDONE], &work);
	while (work) {
		msg = &work->msg;
		instance = msg->instance;
		fcount = msg->param1;
		rcount = msg->param2;
		status = msg->param3;

		if (instance >= FIMC_IS_STREAM_COUNT) {
			err("instance is invalid(%d)", instance);
			goto p_err;
		}

		device = &((struct fimc_is_core *)itf->core)->ischain[instance];
		if (!device) {
			err("device is NULL");
			goto p_err;
		}

		if (!test_bit(FIMC_IS_ISCHAIN_OPEN, &device->state)) {
			merr("device is not open", device);
			goto p_err;
		}

		subdev = &device->ixc;
		if (!test_bit(FIMC_IS_SUBDEV_START, &subdev->state)) {
			merr("subdev is not start", device);
			goto p_err;
		}

		leader = subdev->leader;
		if (!leader) {
			merr("leader is NULL", device);
			goto p_err;
		}

		wq_func_frame(leader, subdev, fcount, rcount, status);

p_err:
		set_free_work(&itf->work_list[WORK_I0C_FDONE], work);
		get_req_work(&itf->work_list[WORK_I0C_FDONE], &work);
	}
}

static void wq_func_i0p(struct work_struct *data)
{
	u32 instance, fcount, rcount, status;
	struct fimc_is_interface *itf;
	struct fimc_is_device_ischain *device;
	struct fimc_is_subdev *leader, *subdev;
	struct fimc_is_work *work;
	struct fimc_is_msg *msg;

	itf = container_of(data, struct fimc_is_interface, work_wq[WORK_I0P_FDONE]);

	get_req_work(&itf->work_list[WORK_I0P_FDONE], &work);
	while (work) {
		msg = &work->msg;
		instance = msg->instance;
		fcount = msg->param1;
		rcount = msg->param2;
		status = msg->param3;

		if (instance >= FIMC_IS_STREAM_COUNT) {
			err("instance is invalid(%d)", instance);
			goto p_err;
		}

		device = &((struct fimc_is_core *)itf->core)->ischain[instance];
		if (!device) {
			err("device is NULL");
			goto p_err;
		}

		if (!test_bit(FIMC_IS_ISCHAIN_OPEN, &device->state)) {
			merr("device is not open", device);
			goto p_err;
		}

		subdev = &device->ixp;
		if (!test_bit(FIMC_IS_SUBDEV_START, &subdev->state)) {
			merr("subdev is not start", device);
			goto p_err;
		}

		leader = subdev->leader;
		if (!leader) {
			merr("leader is NULL", device);
			goto p_err;
		}

		wq_func_frame(leader, subdev, fcount, rcount, status);

p_err:
		set_free_work(&itf->work_list[WORK_I0P_FDONE], work);
		get_req_work(&itf->work_list[WORK_I0P_FDONE], &work);
	}
}

static void wq_func_i1c(struct work_struct *data)
{
	u32 instance, fcount, rcount, status;
	struct fimc_is_interface *itf;
	struct fimc_is_device_ischain *device;
	struct fimc_is_subdev *leader, *subdev;
	struct fimc_is_work *work;
	struct fimc_is_msg *msg;

	itf = container_of(data, struct fimc_is_interface, work_wq[WORK_I1C_FDONE]);

	get_req_work(&itf->work_list[WORK_I1C_FDONE], &work);
	while (work) {
		msg = &work->msg;
		instance = msg->instance;
		fcount = msg->param1;
		rcount = msg->param2;
		status = msg->param3;

		if (instance >= FIMC_IS_STREAM_COUNT) {
			err("instance is invalid(%d)", instance);
			goto p_err;
		}

		device = &((struct fimc_is_core *)itf->core)->ischain[instance];
		if (!device) {
			err("device is NULL");
			goto p_err;
		}

		if (!test_bit(FIMC_IS_ISCHAIN_OPEN, &device->state)) {
			merr("device is not open", device);
			goto p_err;
		}

		subdev = &device->ixc;
		if (!test_bit(FIMC_IS_SUBDEV_START, &subdev->state)) {
			merr("subdev is not start", device);
			goto p_err;
		}

		leader = subdev->leader;
		if (!leader) {
			merr("leader is NULL", device);
			goto p_err;
		}

		wq_func_frame(leader, subdev, fcount, rcount, status);

p_err:
		set_free_work(&itf->work_list[WORK_I1C_FDONE], work);
		get_req_work(&itf->work_list[WORK_I1C_FDONE], &work);
	}
}

static void wq_func_i1p(struct work_struct *data)
{
	u32 instance, fcount, rcount, status;
	struct fimc_is_interface *itf;
	struct fimc_is_device_ischain *device;
	struct fimc_is_subdev *leader, *subdev;
	struct fimc_is_work *work;
	struct fimc_is_msg *msg;

	itf = container_of(data, struct fimc_is_interface, work_wq[WORK_I1P_FDONE]);

	get_req_work(&itf->work_list[WORK_I1P_FDONE], &work);
	while (work) {
		msg = &work->msg;
		instance = msg->instance;
		fcount = msg->param1;
		rcount = msg->param2;
		status = msg->param3;

		if (instance >= FIMC_IS_STREAM_COUNT) {
			err("instance is invalid(%d)", instance);
			goto p_err;
		}

		device = &((struct fimc_is_core *)itf->core)->ischain[instance];
		if (!device) {
			err("device is NULL");
			goto p_err;
		}

		if (!test_bit(FIMC_IS_ISCHAIN_OPEN, &device->state)) {
			merr("device is not open", device);
			goto p_err;
		}

		subdev = &device->ixp;
		if (!test_bit(FIMC_IS_SUBDEV_START, &subdev->state)) {
			merr("subdev is not start", device);
			goto p_err;
		}

		leader = subdev->leader;
		if (!leader) {
			merr("leader is NULL", device);
			goto p_err;
		}

		wq_func_frame(leader, subdev, fcount, rcount, status);

p_err:
		set_free_work(&itf->work_list[WORK_I1P_FDONE], work);
		get_req_work(&itf->work_list[WORK_I1P_FDONE], &work);
	}
}

static void wq_func_scc(struct work_struct *data)
{
	u32 instance, fcount, rcount, status;
	struct fimc_is_interface *itf;
	struct fimc_is_device_ischain *device;
	struct fimc_is_subdev *leader, *subdev;
	struct fimc_is_work *work;
	struct fimc_is_msg *msg;

	itf = container_of(data, struct fimc_is_interface, work_wq[WORK_SCC_FDONE]);

	get_req_work(&itf->work_list[WORK_SCC_FDONE], &work);
	while (work) {
		msg = &work->msg;
		instance = msg->instance;
		fcount = msg->param1;
		rcount = msg->param2;
		status = msg->param3;

		if (instance >= FIMC_IS_STREAM_COUNT) {
			err("instance is invalid(%d)", instance);
			goto p_err;
		}

		device = &((struct fimc_is_core *)itf->core)->ischain[instance];
		if (!device) {
			err("device is NULL");
			goto p_err;
		}

		if (!test_bit(FIMC_IS_ISCHAIN_OPEN, &device->state)) {
			merr("device is not open", device);
			goto p_err;
		}

		subdev = &device->scc;
		if (!test_bit(FIMC_IS_SUBDEV_START, &subdev->state)) {
			merr("subdev is not start", device);
			goto p_err;
		}

		leader = subdev->leader;
		if (!leader) {
			merr("leader is NULL", device);
			goto p_err;
		}

		wq_func_frame(leader, subdev, fcount, rcount, status);

p_err:
		set_free_work(&itf->work_list[WORK_SCC_FDONE], work);
		get_req_work(&itf->work_list[WORK_SCC_FDONE], &work);
	}
}

static void wq_func_scp(struct work_struct *data)
{
	u32 instance, fcount, rcount, status;
	struct fimc_is_interface *itf;
	struct fimc_is_device_ischain *device;
	struct fimc_is_subdev *leader, *subdev;
	struct fimc_is_work *work;
	struct fimc_is_msg *msg;

	itf = container_of(data, struct fimc_is_interface, work_wq[WORK_SCP_FDONE]);

	get_req_work(&itf->work_list[WORK_SCP_FDONE], &work);
	while (work) {
		msg = &work->msg;
		instance = msg->instance;
		fcount = msg->param1;
		rcount = msg->param2;
		status = msg->param3;

		if (instance >= FIMC_IS_STREAM_COUNT) {
			err("instance is invalid(%d)", instance);
			goto p_err;
		}

		device = &((struct fimc_is_core *)itf->core)->ischain[instance];
		if (!device) {
			err("device is NULL");
			goto p_err;
		}

		if (!test_bit(FIMC_IS_ISCHAIN_OPEN, &device->state)) {
			merr("device is not open", device);
			goto p_err;
		}

		subdev = &device->scp;
		if (!test_bit(FIMC_IS_SUBDEV_START, &subdev->state)) {
			merr("subdev is not start", device);
			goto p_err;
		}

		leader = subdev->leader;
		if (!leader) {
			merr("leader is NULL", device);
			goto p_err;
		}

		wq_func_frame(leader, subdev, fcount, rcount, status);

p_err:
		set_free_work(&itf->work_list[WORK_SCP_FDONE], work);
		get_req_work(&itf->work_list[WORK_SCP_FDONE], &work);
	}
}

static void wq_func_group_30s(struct fimc_is_groupmgr *groupmgr,
	struct fimc_is_group *group,
	struct fimc_is_framemgr *framemgr,
	struct fimc_is_frame *frame,
	struct fimc_is_video_ctx *vctx,
	u32 status, u32 lindex, u32 hindex)
{
	u32 done_state = VB2_BUF_STATE_DONE;

	BUG_ON(!vctx);
	BUG_ON(!framemgr);
	BUG_ON(!frame);

	if (status) {
		mgrinfo("[ERR] NDONE(%d, E%X(L%X H%X))\n", group, group, frame, frame->index, status,
			lindex, hindex);
		done_state = VB2_BUF_STATE_ERROR;

		/* specially force set is enabled when perframe control is fail */
		if (lindex || hindex)
			set_bit(FIMC_IS_SUBDEV_FORCE_SET, &group->leader.state);
	}

#ifdef DBG_STREAMING
	if (!status)
		mgrinfo(" DONE(%d)\n", group, group, frame, frame->index);
#endif

	/* 2. leader frame done */
	fimc_is_ischain_meta_invalid(frame);

	frame->result = status;
	clear_bit(group->leader.id, &frame->out_flag);
	fimc_is_frame_trans_pro_to_com(framemgr, frame);
	fimc_is_group_done(groupmgr, group, frame, done_state);
	buffer_done(vctx, frame->index, done_state);
}

static void wq_func_group_31s(struct fimc_is_groupmgr *groupmgr,
	struct fimc_is_group *group,
	struct fimc_is_framemgr *framemgr,
	struct fimc_is_frame *frame,
	struct fimc_is_video_ctx *vctx,
	u32 status, u32 lindex, u32 hindex)
{
	u32 done_state = VB2_BUF_STATE_DONE;

	BUG_ON(!vctx);
	BUG_ON(!framemgr);
	BUG_ON(!frame);

	if (status) {
		mgrinfo("[ERR] NDONE(%d, E%X(L%X H%X))\n", group, group, frame, frame->index, status,
			lindex, hindex);
		done_state = VB2_BUF_STATE_ERROR;

		/* specially force set is enabled when perframe control is fail */
		if (lindex || hindex)
			set_bit(FIMC_IS_SUBDEV_FORCE_SET, &group->leader.state);
	}

#ifdef DBG_STREAMING
	if (!status)
		mgrinfo(" DONE(%d)\n", group, group, frame, frame->index);
#endif

	/* 2. leader frame done */
	fimc_is_ischain_meta_invalid(frame);

	frame->result = status;
	clear_bit(group->leader.id, &frame->out_flag);
	fimc_is_frame_trans_pro_to_com(framemgr, frame);
	fimc_is_group_done(groupmgr, group, frame, done_state);
	buffer_done(vctx, frame->index, done_state);
}

static void wq_func_group_i0s(struct fimc_is_groupmgr *groupmgr,
	struct fimc_is_group *group,
	struct fimc_is_framemgr *framemgr,
	struct fimc_is_frame *frame,
	struct fimc_is_video_ctx *vctx,
	u32 status, u32 lindex, u32 hindex)
{
	u32 done_state = VB2_BUF_STATE_DONE;

	BUG_ON(!framemgr);
	BUG_ON(!frame);

	if (status) {
		mgrinfo("[ERR] NDONE(%d, E%X(L%X H%X))\n", group, group, frame, frame->index, status,
			lindex, hindex);
		done_state = VB2_BUF_STATE_ERROR;

		/* specially force set is enabled when perframe control is fail */
		if (lindex || hindex)
			set_bit(FIMC_IS_SUBDEV_FORCE_SET, &group->leader.state);
	}

#ifdef DBG_STREAMING
	if (!status)
		mgrinfo(" DONE(%d)\n", group, group, frame, frame->index);
#endif

	/* Cache Invalidation */
	fimc_is_ischain_meta_invalid(frame);

	frame->result = status;
	clear_bit(group->leader.id, &frame->out_flag);
	fimc_is_frame_trans_pro_to_com(framemgr, frame);
	fimc_is_group_done(groupmgr, group, frame, done_state);
	buffer_done(vctx, frame->index, done_state);
}

static void wq_func_group_i1s(struct fimc_is_groupmgr *groupmgr,
	struct fimc_is_group *group,
	struct fimc_is_framemgr *framemgr,
	struct fimc_is_frame *frame,
	struct fimc_is_video_ctx *vctx,
	u32 status, u32 lindex, u32 hindex)
{
	u32 done_state = VB2_BUF_STATE_DONE;

	BUG_ON(!framemgr);
	BUG_ON(!frame);

	if (status) {
		mgrinfo("[ERR] NDONE(%d, E%X(L%X H%X))\n", group, group, frame, frame->index, status,
			lindex, hindex);
		done_state = VB2_BUF_STATE_ERROR;

		/* specially force set is enabled when perframe control is fail */
		if (lindex || hindex)
			set_bit(FIMC_IS_SUBDEV_FORCE_SET, &group->leader.state);
	}

#ifdef DBG_STREAMING
	if (!status)
		mgrinfo(" DONE(%d)\n", group, group, frame, frame->index);
#endif

	/* Cache Invalidation */
	fimc_is_ischain_meta_invalid(frame);

	frame->result = status;
	clear_bit(group->leader.id, &frame->out_flag);
	fimc_is_frame_trans_pro_to_com(framemgr, frame);
	fimc_is_group_done(groupmgr, group, frame, done_state);
	buffer_done(vctx, frame->index, done_state);
}

static void wq_func_group_dis(struct fimc_is_groupmgr *groupmgr,
	struct fimc_is_group *group,
	struct fimc_is_framemgr *framemgr,
	struct fimc_is_frame *frame,
	struct fimc_is_video_ctx *vctx,
	u32 status, u32 lindex, u32 hindex)
{
	u32 done_state = VB2_BUF_STATE_DONE;

	BUG_ON(!framemgr);
	BUG_ON(!frame);

	if (status) {
		mgrinfo("[ERR] NDONE(%d, E%X(L%X H%X))\n", group, group, frame, frame->index, status,
			lindex, hindex);
		done_state = VB2_BUF_STATE_ERROR;

		/* specially force set is enabled when perframe control is fail */
		if (lindex || hindex)
			set_bit(FIMC_IS_SUBDEV_FORCE_SET, &group->leader.state);
	}

#ifdef DBG_STREAMING
	if (!status)
		mgrinfo(" DONE(%d)\n", group, group, frame, frame->index);
#endif

	/* Cache Invalidation */
	fimc_is_ischain_meta_invalid(frame);

	frame->result = status;
	clear_bit(group->leader.id, &frame->out_flag);
	fimc_is_frame_trans_pro_to_com(framemgr, frame);
	fimc_is_group_done(groupmgr, group, frame, done_state);
	buffer_done(vctx, frame->index, done_state);
}

void wq_func_group(struct fimc_is_device_ischain *device,
	struct fimc_is_groupmgr *groupmgr,
	struct fimc_is_group *group,
	struct fimc_is_framemgr *framemgr,
	struct fimc_is_frame *frame,
	struct fimc_is_video_ctx *vctx,
	u32 status, u32 fcount)
{
	u32 lindex = 0;
	u32 hindex = 0;
	void (*wq_func_group_xxx)(struct fimc_is_groupmgr *groupmgr,
		struct fimc_is_group *group,
		struct fimc_is_framemgr *framemgr,
		struct fimc_is_frame *frame,
		struct fimc_is_video_ctx *vctx,
		u32 status, u32 lindex, u32 hindex);

	BUG_ON(!groupmgr);
	BUG_ON(!group);
	BUG_ON(!framemgr);
	BUG_ON(!frame);
	BUG_ON(!vctx);

	/*
	 * complete count should be lower than 3 when
	 * buffer is queued or overflow can be occured
	 */
	if (framemgr->frame_com_cnt >= DIV_ROUND_UP(framemgr->frame_cnt, 2))
		mgwarn(" complete bufs : %d", device, group, (framemgr->frame_com_cnt + 1));

	switch (group->id) {
	case GROUP_ID_3AA0:
		wq_func_group_xxx = wq_func_group_30s;
		break;
	case GROUP_ID_3AA1:
		wq_func_group_xxx = wq_func_group_31s;
		break;
	case GROUP_ID_ISP0:
		wq_func_group_xxx = wq_func_group_i0s;
		break;
	case GROUP_ID_ISP1:
		wq_func_group_xxx = wq_func_group_i1s;
		break;
	case GROUP_ID_DIS0:
		wq_func_group_xxx = wq_func_group_dis;
		break;
	default:
		merr("unresolved group id %d", device, group->id);
		BUG();
	}

	if (status) {
		lindex = frame->shot->ctl.vendor_entry.lowIndexParam;
		lindex &= ~frame->shot->dm.vendor_entry.lowIndexParam;
		hindex = frame->shot->ctl.vendor_entry.highIndexParam;
		hindex &= ~frame->shot->dm.vendor_entry.highIndexParam;
	}

	if (unlikely(fcount != frame->fcount)) {
		if (test_bit(FIMC_IS_GROUP_OTF_INPUT, &group->state)) {
			while (frame) {
				if (fcount == frame->fcount) {
					wq_func_group_xxx(groupmgr, group, framemgr, frame, vctx,
						status, lindex, hindex);
					break;
				} else if (fcount > frame->fcount) {
					wq_func_group_xxx(groupmgr, group, framemgr, frame, vctx,
						SHOT_ERR_MISMATCH, lindex, hindex);

					/* get next leader frame */
					fimc_is_frame_process_head(framemgr, &frame);
				} else {
					warn("%d shot done is ignored", fcount);
					break;
				}
			}
		} else {
			wq_func_group_xxx(groupmgr, group, framemgr, frame, vctx,
				SHOT_ERR_MISMATCH, lindex, hindex);
		}
	} else {
		wq_func_group_xxx(groupmgr, group, framemgr, frame, vctx,
			status, lindex, hindex);
	}
}

static void wq_func_shot(struct work_struct *data)
{
	struct fimc_is_device_ischain *device;
	struct fimc_is_interface *itf;
	struct fimc_is_msg *msg;
	struct fimc_is_framemgr *framemgr;
	struct fimc_is_frame *frame;
	struct fimc_is_groupmgr *groupmgr;
	struct fimc_is_group *group;
	struct fimc_is_work_list *work_list;
	struct fimc_is_work *work;
	struct fimc_is_video_ctx *vctx;
	unsigned long flags;
	u32 fcount, status;
	int instance;
	int group_id;
	struct fimc_is_core *core;

	BUG_ON(!data);

	itf = container_of(data, struct fimc_is_interface, work_wq[INTR_SHOT_DONE]);
	work_list = &itf->work_list[INTR_SHOT_DONE];
	group  = NULL;
	vctx = NULL;
	framemgr = NULL;

	get_req_work(work_list, &work);
	while (work) {
		core = (struct fimc_is_core *)itf->core;
		instance = work->msg.instance;
		group_id = work->msg.group;
		device = &((struct fimc_is_core *)itf->core)->ischain[instance];
		groupmgr = device->groupmgr;

		msg = &work->msg;
		fcount = msg->param1;
		status = msg->param2;

		switch (group_id) {
		case GROUP_ID(GROUP_ID_3AA0):
		case GROUP_ID(GROUP_ID_3AA1):
			group = &device->group_3aa;
			break;
		case GROUP_ID(GROUP_ID_ISP0):
		case GROUP_ID(GROUP_ID_ISP1):
			group = &device->group_isp;
			break;
		case GROUP_ID(GROUP_ID_DIS0):
			group = &device->group_dis;
			break;
		default:
			merr("unresolved group id %d", device, group_id);
			group = NULL;
			vctx = NULL;
			framemgr = NULL;
			goto remain;
		}

		if (!group) {
			merr("group is NULL", device);
			goto remain;
		}

		vctx = group->leader.vctx;
		if (!vctx) {
			merr("vctx is NULL", device);
			goto remain;
		}

		framemgr = GET_FRAMEMGR(vctx);
		if (!framemgr) {
			merr("framemgr is NULL", device);
			goto remain;
		}

		framemgr_e_barrier_irqs(framemgr, FMGR_IDX_5, flags);

		fimc_is_frame_process_head(framemgr, &frame);

		if (frame) {
			PROGRAM_COUNT(13);
#ifdef MEASURE_TIME
#ifdef EXTERNAL_TIME
			do_gettimeofday(&frame->tzone[TM_SHOT_D]);
#endif
#endif

#ifdef ENABLE_CLOCK_GATE
			/* dynamic clock off */
			if (sysfs_debug.en_clk_gate &&
					sysfs_debug.clk_gate_mode == CLOCK_GATE_MODE_HOST)
				fimc_is_clk_gate_set(core, group->id, false, false, true);
#endif
			wq_func_group(device, groupmgr, group, framemgr, frame,
				vctx, status, fcount);

			PROGRAM_COUNT(14);
		} else {
			mgerr("invalid shot done(%d)", device, group, fcount);
			fimc_is_frame_print_all(framemgr);
		}

		framemgr_x_barrier_irqr(framemgr, FMGR_IDX_5, flags);
#ifdef ENABLE_CLOCK_GATE
		if (fcount == 1 &&
				sysfs_debug.en_clk_gate &&
				sysfs_debug.clk_gate_mode == CLOCK_GATE_MODE_HOST)
			fimc_is_clk_gate_lock_set(core, instance, false);
#endif
remain:
		set_free_work(work_list, work);
		get_req_work(work_list, &work);
	}
}

static inline void print_framemgr_spinlock_usage(struct fimc_is_core *core)
{
	u32 i;
	struct fimc_is_device_ischain *ischain;
	struct fimc_is_device_sensor *sensor;
	struct fimc_is_framemgr *framemgr;
	struct fimc_is_subdev *subdev;

	for (i = 0; i < FIMC_IS_SENSOR_COUNT; ++i) {
		sensor = &core->sensor[i];
		if (test_bit(FIMC_IS_SENSOR_OPEN, &sensor->state) && (framemgr = GET_SUBDEV_FRAMEMGR(sensor)))
			info("[@] framemgr(0x%08X) sindex : 0x%08lX\n", framemgr->id, framemgr->sindex);
	}

	for (i = 0; i < FIMC_IS_STREAM_COUNT; ++i) {
		ischain = &core->ischain[i];
		if (test_bit(FIMC_IS_ISCHAIN_OPEN, &ischain->state)) {
			/* 3AA GROUP */
			subdev = &ischain->group_3aa.leader;
			if (test_bit(FIMC_IS_SUBDEV_OPEN, &subdev->state) && (framemgr = GET_SUBDEV_FRAMEMGR(subdev)))
				info("[@] framemgr(0x%08X) sindex : 0x%08lX\n", framemgr->id, framemgr->sindex);

			subdev = &ischain->txc;
			if (test_bit(FIMC_IS_SUBDEV_OPEN, &subdev->state) && (framemgr = GET_SUBDEV_FRAMEMGR(subdev)))
				info("[@] framemgr(0x%08X) sindex : 0x%08lX\n", framemgr->id, framemgr->sindex);

			subdev = &ischain->txp;
			if (test_bit(FIMC_IS_SUBDEV_OPEN, &subdev->state) && (framemgr = GET_SUBDEV_FRAMEMGR(subdev)))
				info("[@] framemgr(0x%08X) sindex : 0x%08lX\n", framemgr->id, framemgr->sindex);

			/* ISP GROUP */
			subdev = &ischain->group_isp.leader;
			if (test_bit(FIMC_IS_SUBDEV_OPEN, &subdev->state) && (framemgr = GET_SUBDEV_FRAMEMGR(subdev)))
				info("[@] framemgr(0x%08X) sindex : 0x%08lX\n", framemgr->id, framemgr->sindex);

			subdev = &ischain->ixc;
			if (test_bit(FIMC_IS_SUBDEV_OPEN, &subdev->state) && (framemgr = GET_SUBDEV_FRAMEMGR(subdev)))
				info("[@] framemgr(0x%08X) sindex : 0x%08lX\n", framemgr->id, framemgr->sindex);

			subdev = &ischain->ixp;
			if (test_bit(FIMC_IS_SUBDEV_OPEN, &subdev->state) && (framemgr = GET_SUBDEV_FRAMEMGR(subdev)))
				info("[@] framemgr(0x%08X) sindex : 0x%08lX\n", framemgr->id, framemgr->sindex);

			/* DIS GROUP */
			subdev = &ischain->group_dis.leader;
			if (test_bit(FIMC_IS_SUBDEV_OPEN, &subdev->state) && (framemgr = GET_SUBDEV_FRAMEMGR(subdev)))
				info("[@] framemgr(0x%08X) sindex : 0x%08lX\n", framemgr->id, framemgr->sindex);

			subdev = &ischain->scc;
			if (test_bit(FIMC_IS_SUBDEV_OPEN, &subdev->state) && (framemgr = GET_SUBDEV_FRAMEMGR(subdev)))
				info("[@] framemgr(0x%08X) sindex : 0x%08lX\n", framemgr->id, framemgr->sindex);

			subdev = &ischain->scp;
			if (test_bit(FIMC_IS_SUBDEV_OPEN, &subdev->state) && (framemgr = GET_SUBDEV_FRAMEMGR(subdev)))
				info("[@] framemgr(0x%08X) sindex : 0x%08lX\n", framemgr->id, framemgr->sindex);
		}
	}
}

static void interface_timer(unsigned long data)
{
	u32 shot_count, scount_3ax, scount_isp;
	u32 fcount, i;
	unsigned long flags;
	struct fimc_is_interface *itf = (struct fimc_is_interface *)data;
	struct fimc_is_core *core;
	struct fimc_is_device_ischain *device;
	struct fimc_is_device_sensor *sensor;
	struct fimc_is_framemgr *framemgr;
	struct fimc_is_work_list *work_list;
	struct work_struct *work_wq;

	BUG_ON(!itf);
	BUG_ON(!itf->core);

	if (!test_bit(IS_IF_STATE_OPEN, &itf->state)) {
		pr_info("shot timer is terminated\n");
		return;
	}

	core = itf->core;

	for (i = 0; i < FIMC_IS_STREAM_COUNT; ++i) {
		device = &core->ischain[i];
		shot_count = 0;
		scount_3ax = 0;
		scount_isp = 0;

		sensor = device->sensor;
		if (!sensor)
			continue;

		if (!test_bit(FIMC_IS_SENSOR_FRONT_START, &sensor->state))
			continue;

		if (test_bit(FIMC_IS_ISCHAIN_OPEN_STREAM, &device->state)) {
			spin_lock_irqsave(&itf->shot_check_lock, flags);
			if (atomic_read(&itf->shot_check[i])) {
				atomic_set(&itf->shot_check[i], 0);
				atomic_set(&itf->shot_timeout[i], 0);
				spin_unlock_irqrestore(&itf->shot_check_lock, flags);
				continue;
			}
			spin_unlock_irqrestore(&itf->shot_check_lock, flags);

			if (test_bit(FIMC_IS_GROUP_START, &device->group_3aa.state)) {
				framemgr = GET_SUBDEV_FRAMEMGR(&device->group_3aa.leader);
				framemgr_e_barrier_irqs(framemgr, FMGR_IDX_6, flags);
				scount_3ax = framemgr->frame_pro_cnt;
				shot_count += scount_3ax;
				framemgr_x_barrier_irqr(framemgr, FMGR_IDX_6, flags);
			}

			if (test_bit(FIMC_IS_GROUP_START, &device->group_isp.state)) {
				framemgr = GET_SUBDEV_FRAMEMGR(&device->group_isp.leader);
				framemgr_e_barrier_irqs(framemgr, FMGR_IDX_7, flags);
				scount_isp = framemgr->frame_pro_cnt;
				shot_count += scount_isp;
				framemgr_x_barrier_irqr(framemgr, FMGR_IDX_7, flags);
			}

			if (test_bit(FIMC_IS_GROUP_START, &device->group_dis.state)) {
				framemgr = GET_SUBDEV_FRAMEMGR(&device->group_dis.leader);
				framemgr_e_barrier_irqs(framemgr, FMGR_IDX_8, flags);
				shot_count += framemgr->frame_pro_cnt;
				framemgr_x_barrier_irqr(framemgr, FMGR_IDX_8, flags);
			}
		}

		if (shot_count) {
			atomic_inc(&itf->shot_timeout[i]);
			minfo("shot timer[%d] is increased to %d\n", device,
				i, atomic_read(&itf->shot_timeout[i]));
		}

		if (atomic_read(&itf->shot_timeout[i]) > TRY_TIMEOUT_COUNT) {
			merr("shot command is timeout(%d, %d(%d+%d))", device,
				atomic_read(&itf->shot_timeout[i]),
				shot_count, scount_3ax, scount_isp);

			minfo("\n### 3ax framemgr info ###\n", device);
			if (scount_3ax) {
				framemgr = GET_SUBDEV_FRAMEMGR(&device->group_3aa.leader);
				fimc_is_frame_print_all(framemgr);
			}

			minfo("\n### isp framemgr info ###\n", device);
			if (scount_isp) {
				framemgr = GET_SUBDEV_FRAMEMGR(&device->group_isp.leader);
				fimc_is_frame_print_all(framemgr);
			}

			minfo("\n### work list info ###\n", device);
			work_list = &itf->work_list[INTR_SHOT_DONE];
			print_fre_work_list(work_list);
			print_req_work_list(work_list);

			if (work_list->work_request_cnt > 0) {
				pr_err("\n### processing work lately ###\n");
				work_wq = &itf->work_wq[INTR_SHOT_DONE];
				wq_func_shot(work_wq);

				atomic_set(&itf->shot_check[i], 0);
				atomic_set(&itf->shot_timeout[i], 0);
			} else {
				fimc_is_hw_logdump(itf);
				fimc_is_hw_regdump(itf);

				/* framemgr spinlock check */
				print_framemgr_spinlock_usage(core);
#ifdef FW_PANIC_ENABLE
				mdelay(2000);
				panic("[@] camera panic!!!");
#endif
				return;
			}
		}
	}

	for (i = 0; i < FIMC_IS_STREAM_COUNT; ++i) {
		sensor = &core->sensor[i];

		if (!test_bit(FIMC_IS_SENSOR_BACK_START, &sensor->state))
			continue;

		if (!test_bit(FIMC_IS_SENSOR_FRONT_START, &sensor->state))
			continue;

		fcount = fimc_is_sensor_g_fcount(sensor);
		if (fcount == atomic_read(&itf->sensor_check[i])) {
			atomic_inc(&itf->sensor_timeout[i]);
			pr_err ("sensor timer[%d] is increased to %d(fcount : %d)\n", i,
				atomic_read(&itf->sensor_timeout[i]), fcount);
		} else {
			atomic_set(&itf->sensor_timeout[i], 0);
			atomic_set(&itf->sensor_check[i], fcount);
		}

		if (atomic_read(&itf->sensor_timeout[i]) > SENSOR_TIMEOUT_COUNT) {
			merr("sensor is timeout(%d, %d)", sensor,
				atomic_read(&itf->sensor_timeout[i]),
				atomic_read(&itf->sensor_check[i]));

			fimc_is_hw_logdump(itf);
			fimc_is_hw_regdump(itf);

			/* framemgr spinlock check */
			print_framemgr_spinlock_usage(core);

			/* clock & gpio dump */
			CALL_POPS(device, print_clk);
#ifdef SENSOR_PANIC_ENABLE
			mdelay(2000);
			panic("[@] camera sensor panic!!!");
#endif
			return;
		}
	}

	mod_timer(&itf->timer, jiffies + (FIMC_IS_COMMAND_TIMEOUT/TRY_TIMEOUT_COUNT));
}

static irqreturn_t interface_isr(int irq, void *data)
{
	struct fimc_is_interface *itf;
	struct work_struct *work_wq;
	struct fimc_is_work_list *work_list;
	struct fimc_is_work *work;
	u32 status;

	itf = (struct fimc_is_interface *)data;
	status = fimc_is_get_intr(itf);

	if (status & (1<<INTR_SHOT_DONE)) {
		work_wq = &itf->work_wq[WORK_SHOT_DONE];
		work_list = &itf->work_list[WORK_SHOT_DONE];

		get_free_work_irq(work_list, &work);
		if (work) {
			fimc_is_get_cmd(itf, &work->msg, INTR_SHOT_DONE);
			work->fcount = work->msg.param1;
			set_req_work_irq(work_list, work);

			if (!work_pending(work_wq))
				wq_func_schedule(itf, work_wq);
		} else {
			err("shot free work is empty");
		}

		status &= ~(1<<INTR_SHOT_DONE);
		fimc_is_clr_intr(itf, INTR_SHOT_DONE);
	}

	if (status & (1<<INTR_GENERAL)) {
		work_wq = &itf->work_wq[WORK_GENERAL];
		work_list = &itf->work_list[WORK_GENERAL];

		get_free_work_irq(work_list, &work);
		if (work) {
			fimc_is_get_cmd(itf, &work->msg, INTR_GENERAL);
			set_req_work_irq(work_list, work);

			if (!work_pending(work_wq))
				wq_func_schedule(itf, work_wq);
		} else {
			err("general free work is empty");
		}

		status &= ~(1<<INTR_GENERAL);
		fimc_is_clr_intr(itf, INTR_GENERAL);
	}

	if (status & (1<<INTR_30C_FDONE)) {
		work_wq = &itf->work_wq[WORK_30C_FDONE];
		work_list = &itf->work_list[WORK_30C_FDONE];

		get_free_work_irq(work_list, &work);
		if (work) {
			fimc_is_get_cmd(itf, &work->msg, INTR_30C_FDONE);
			set_req_work_irq(work_list, work);

			if (!work_pending(work_wq))
				wq_func_schedule(itf, work_wq);
		} else {
			err("30c free work is empty");
		}

		status &= ~(1<<INTR_30C_FDONE);
		fimc_is_clr_intr(itf, INTR_30C_FDONE);
	}

	if (status & (1<<INTR_30P_FDONE)) {
		work_wq = &itf->work_wq[WORK_30P_FDONE];
		work_list = &itf->work_list[WORK_30P_FDONE];

		get_free_work_irq(work_list, &work);
		if (work) {
			fimc_is_get_cmd(itf, &work->msg, INTR_30P_FDONE);
			set_req_work_irq(work_list, work);

			if (!work_pending(work_wq))
				wq_func_schedule(itf, work_wq);
		} else {
			err("30p free work is empty");
		}

		status &= ~(1<<INTR_30P_FDONE);
		fimc_is_clr_intr(itf, INTR_30P_FDONE);
	}

	if (status & (1<<INTR_31C_FDONE)) {
		work_wq = &itf->work_wq[WORK_31C_FDONE];
		work_list = &itf->work_list[WORK_31C_FDONE];

		get_free_work_irq(work_list, &work);
		if (work) {
			fimc_is_get_cmd(itf, &work->msg, INTR_31C_FDONE);
			set_req_work_irq(work_list, work);

			if (!work_pending(work_wq))
				wq_func_schedule(itf, work_wq);
		} else {
			err("31c free work is empty");
		}

		status &= ~(1<<INTR_31C_FDONE);
		fimc_is_clr_intr(itf, INTR_31C_FDONE);
	}

	if (status & (1<<INTR_31P_FDONE)) {
		work_wq = &itf->work_wq[WORK_31P_FDONE];
		work_list = &itf->work_list[WORK_31P_FDONE];

		get_free_work_irq(work_list, &work);
		if (work) {
			fimc_is_get_cmd(itf, &work->msg, INTR_31P_FDONE);
			set_req_work_irq(work_list, work);

			if (!work_pending(work_wq))
				wq_func_schedule(itf, work_wq);
		} else {
			err("31p free work is empty");
		}

		status &= ~(1<<INTR_31P_FDONE);
		fimc_is_clr_intr(itf, INTR_31P_FDONE);
	}

	if (status & (1<<INTR_I0X_FDONE)) {
		if (itf->com_regs->i0x_param3 == 1) {
			work_wq = &itf->work_wq[WORK_I0C_FDONE];
			work_list = &itf->work_list[WORK_I0C_FDONE];
		} else {
			work_wq = &itf->work_wq[WORK_I0P_FDONE];
			work_list = &itf->work_list[WORK_I0P_FDONE];
		}

		get_free_work_irq(work_list, &work);
		if (work) {
			fimc_is_get_cmd(itf, &work->msg, INTR_I0X_FDONE);
			set_req_work_irq(work_list, work);

			if (!work_pending(work_wq))
				wq_func_schedule(itf, work_wq);
		} else {
			err("i0x free work is empty");
		}

		status &= ~(1<<INTR_I0X_FDONE);
		fimc_is_clr_intr(itf, INTR_I0X_FDONE);
	}

	if (status & (1<<INTR_I1X_FDONE)) {
		if (itf->com_regs->i1x_param3 == 1) {
			work_wq = &itf->work_wq[WORK_I1C_FDONE];
			work_list = &itf->work_list[WORK_I1C_FDONE];
		} else {
			work_wq = &itf->work_wq[WORK_I1P_FDONE];
			work_list = &itf->work_list[WORK_I1P_FDONE];
		}

		get_free_work_irq(work_list, &work);
		if (work) {
			fimc_is_get_cmd(itf, &work->msg, INTR_I1X_FDONE);
			set_req_work_irq(work_list, work);

			if (!work_pending(work_wq))
				wq_func_schedule(itf, work_wq);
		} else {
			err("i1x free work is empty");
		}

		status &= ~(1<<INTR_I1X_FDONE);
		fimc_is_clr_intr(itf, INTR_I1X_FDONE);
	}

	if (status & (1<<INTR_SCX_FDONE)) {
		if (itf->com_regs->scx_param3 == 1) {
			work_wq = &itf->work_wq[WORK_SCC_FDONE];
			work_list = &itf->work_list[WORK_SCC_FDONE];
		} else {
			work_wq = &itf->work_wq[WORK_SCP_FDONE];
			work_list = &itf->work_list[WORK_SCP_FDONE];
		}

		get_free_work_irq(work_list, &work);
		if (work) {
			fimc_is_get_cmd(itf, &work->msg, INTR_SCX_FDONE);
			set_req_work_irq(work_list, work);

			if (!work_pending(work_wq))
				wq_func_schedule(itf, work_wq);
		} else {
			err("scx free work is empty");
		}

		status &= ~(1<<INTR_SCX_FDONE);
		fimc_is_clr_intr(itf, INTR_SCX_FDONE);
	}

	if (status != 0)
		err("status is NOT all clear(0x%08X)", status);

	return IRQ_HANDLED;
}

#define VERSION_OF_NO_NEED_IFLAG 221
int fimc_is_interface_probe(struct fimc_is_interface *this,
	struct fimc_is_minfo *minfo,
	ulong regs,
	u32 irq,
	void *core_data)
{
	int ret = 0;
	struct fimc_is_core *core = (struct fimc_is_core *)core_data;

	dbg_interface("%s\n", __func__);

	init_request_barrier(this);
	init_process_barrier(this);
	init_waitqueue_head(&this->lock_wait_queue);
	init_waitqueue_head(&this->init_wait_queue);
	init_waitqueue_head(&this->idle_wait_queue);
	spin_lock_init(&this->shot_check_lock);

	this->workqueue = alloc_workqueue("fimc-is/highpri", WQ_HIGHPRI, 0);
	if (!this->workqueue)
		probe_warn("failed to alloc own workqueue, will be use global one");

	INIT_WORK(&this->work_wq[WORK_GENERAL], wq_func_general);
	INIT_WORK(&this->work_wq[WORK_SHOT_DONE], wq_func_shot);
	INIT_WORK(&this->work_wq[WORK_30C_FDONE], wq_func_30c);
	INIT_WORK(&this->work_wq[WORK_30P_FDONE], wq_func_30p);
	INIT_WORK(&this->work_wq[WORK_31C_FDONE], wq_func_31c);
	INIT_WORK(&this->work_wq[WORK_31P_FDONE], wq_func_31p);
	INIT_WORK(&this->work_wq[WORK_I0C_FDONE], wq_func_i0c);
	INIT_WORK(&this->work_wq[WORK_I0P_FDONE], wq_func_i0p);
	INIT_WORK(&this->work_wq[WORK_I1C_FDONE], wq_func_i1c);
	INIT_WORK(&this->work_wq[WORK_I1P_FDONE], wq_func_i1p);
	INIT_WORK(&this->work_wq[WORK_SCC_FDONE], wq_func_scc);
	INIT_WORK(&this->work_wq[WORK_SCP_FDONE], wq_func_scp);

	this->regs = (void *)regs;
	this->com_regs = (struct is_common_reg *)(regs + ISSR0);

	this->itf_kvaddr = minfo->kvaddr;
	this->first_launch = false;
#ifdef CONFIG_USE_VENDER_FEATURE
	this->need_cold_reset = false;
#endif
	ret = request_irq(irq, interface_isr, 0, "mcuctl", this);
	if (ret)
		probe_err("request_irq failed\n");

	notify_fcount_sen0		= &this->com_regs->fcount_sen0;
	notify_fcount_sen1		= &this->com_regs->fcount_sen1;
	notify_fcount_sen2		= &this->com_regs->fcount_sen2;
	last_fcount1			= &this->com_regs->fcount_sen3;
	last_fcount0			= &this->com_regs->fcount_sen3;
	this->core			= (void *)core;
	clear_bit(IS_IF_STATE_OPEN, &this->state);
	clear_bit(IS_IF_STATE_START, &this->state);
	clear_bit(IS_IF_STATE_BUSY, &this->state);
	clear_bit(IS_IF_STATE_READY, &this->state);
	clear_bit(IS_IF_STATE_LOGGING, &this->state);
	clear_bit(IS_IF_RESUME, &this->fw_boot);
	clear_bit(IS_IF_SUSPEND, &this->fw_boot);

	init_work_list(&this->nblk_cam_ctrl, TRACE_WORK_ID_CAMCTRL, MAX_NBLOCKING_COUNT);
	init_work_list(&this->work_list[WORK_GENERAL], TRACE_WORK_ID_GENERAL, MAX_WORK_COUNT);
	init_work_list(&this->work_list[WORK_SHOT_DONE], TRACE_WORK_ID_SHOT, MAX_WORK_COUNT);
	init_work_list(&this->work_list[WORK_30C_FDONE], TRACE_WORK_ID_30C, MAX_WORK_COUNT);
	init_work_list(&this->work_list[WORK_30P_FDONE], TRACE_WORK_ID_30P, MAX_WORK_COUNT);
	init_work_list(&this->work_list[WORK_31C_FDONE], TRACE_WORK_ID_31C, MAX_WORK_COUNT);
	init_work_list(&this->work_list[WORK_31P_FDONE], TRACE_WORK_ID_31P, MAX_WORK_COUNT);
	init_work_list(&this->work_list[WORK_I0C_FDONE], TRACE_WORK_ID_I0C, MAX_WORK_COUNT);
	init_work_list(&this->work_list[WORK_I0P_FDONE], TRACE_WORK_ID_I0P, MAX_WORK_COUNT);
	init_work_list(&this->work_list[WORK_I1C_FDONE], TRACE_WORK_ID_I1C, MAX_WORK_COUNT);
	init_work_list(&this->work_list[WORK_I1P_FDONE], TRACE_WORK_ID_I1P, MAX_WORK_COUNT);
	init_work_list(&this->work_list[WORK_SCC_FDONE], TRACE_WORK_ID_SCC, MAX_WORK_COUNT);
	init_work_list(&this->work_list[WORK_SCP_FDONE], TRACE_WORK_ID_SCP, MAX_WORK_COUNT);

	this->err_report_vendor = NULL;

#ifdef MEASURE_TIME
#ifdef INTERFACE_TIME
	{
		u32 i;
		for (i = 0; i < HIC_COMMAND_END; ++i)
			measure_init(&this->time[i], i);
	}
#endif
#endif

	return ret;
}

int fimc_is_interface_open(struct fimc_is_interface *this)
{
	int i;
	int ret = 0;

	if (test_bit(IS_IF_STATE_OPEN, &this->state)) {
		err("already open");
		ret = -EMFILE;
		goto exit;
	}

	dbg_interface("%s\n", __func__);

	for (i = 0; i < FIMC_IS_STREAM_COUNT; i++) {
		this->streaming[i] = IS_IF_STREAMING_INIT;
		this->processing[i] = IS_IF_PROCESSING_INIT;
		atomic_set(&this->shot_check[i], 0);
		atomic_set(&this->shot_timeout[i], 0);
		atomic_set(&this->sensor_check[i], 0);
		atomic_set(&this->sensor_timeout[i], 0);
	}

	this->process_msg.command = 0;
	this->request_msg.command = 0;

	this->pdown_ready = IS_IF_POWER_DOWN_READY;
	atomic_set(&this->lock_pid, 0);
	clear_bit(IS_IF_STATE_START, &this->state);
	clear_bit(IS_IF_STATE_BUSY, &this->state);
	clear_bit(IS_IF_STATE_READY, &this->state);
	clear_bit(IS_IF_STATE_LOGGING, &this->state);

	init_timer(&this->timer);
	this->timer.expires = jiffies + (FIMC_IS_COMMAND_TIMEOUT/TRY_TIMEOUT_COUNT);
	this->timer.data = (unsigned long)this;
	this->timer.function = interface_timer;
	add_timer(&this->timer);

	set_bit(IS_IF_STATE_OPEN, &this->state);

exit:
	return ret;
}

int fimc_is_interface_close(struct fimc_is_interface *this)
{
	int ret = 0;
	int retry;

	if (!test_bit(IS_IF_STATE_OPEN, &this->state)) {
		err("already close");
		ret = -EMFILE;
		goto exit;
	}

	retry = 10;
	while (test_busystate(this) && retry) {
		err("interface is busy");
		msleep(20);
		retry--;
	}
	if (!retry)
		err("waiting idle is fail");

	del_timer_sync(&this->timer);
	dbg_interface("%s\n", __func__);

	clear_bit(IS_IF_STATE_OPEN, &this->state);

exit:
	return ret;
}

void fimc_is_interface_lock(struct fimc_is_interface *this)
{
	atomic_set(&this->lock_pid, current->pid);
}

void fimc_is_interface_unlock(struct fimc_is_interface *this)
{
	atomic_set(&this->lock_pid, 0);
	wake_up(&this->lock_wait_queue);
}

void fimc_is_interface_reset(struct fimc_is_interface *this)
{
}

int fimc_is_hw_logdump(struct fimc_is_interface *this)
{
	int debug_cnt, sentence_i;
	char *debug;
	char letter;
	char sentence[250];
	int count = 0, i;
	struct fimc_is_core *core;
	struct fimc_is_minfo *minfo;

	if (!test_bit(IS_IF_STATE_OPEN, &this->state)) {
		warn("interface is closed");
		count = -EINVAL;
		goto p_err;
	}

	if (test_and_set_bit(IS_IF_STATE_LOGGING, &this->state)) {
		warn("already logging");
		while (test_bit(IS_IF_STATE_LOGGING, &this->state)) msleep(100);
		count = 0;
		goto p_err;
	}

	BUG_ON(!this->core);

	pr_err("\n### firmware messsage dump ###\n");

	core = (struct fimc_is_core *)this->core;
	minfo = &core->resourcemgr.minfo;
	sentence_i = 0;

	vb2_ion_sync_for_device(minfo->fw_cookie, DEBUG_OFFSET, DEBUG_CNT, DMA_FROM_DEVICE);

	debug = (char *)(minfo->kvaddr + DEBUG_OFFSET);
	debug_cnt = *((int *)(minfo->kvaddr + DEBUGCTL_OFFSET)) - DEBUG_OFFSET;

	if (fimc_is_debug.debug_cnt > debug_cnt)
		count = (DEBUG_CNT - fimc_is_debug.debug_cnt) + debug_cnt;
	else
		count = debug_cnt - fimc_is_debug.debug_cnt;

	if (count) {
		printk(KERN_DEBUG "start(%d %d)\n", debug_cnt, count);
		for (i = fimc_is_debug.debug_cnt; count > 0; count--) {
			letter = debug[i];
			if (letter) {
				sentence[sentence_i] = letter;
				if (sentence_i >= 247) {
					sentence[sentence_i + 1] = '\n';
					sentence[sentence_i + 2] = 0;
					printk(KERN_DEBUG "%s", sentence);
					sentence_i = 0;
				} else if (letter == '\n') {
					sentence[sentence_i + 1] = 0;
					printk(KERN_DEBUG "%s", sentence);
					sentence_i = 0;
				} else {
					sentence_i++;
				}
			}
			i++;
			if (i > DEBUG_CNT)
				i = 0;
		}
		fimc_is_debug.debug_cnt = debug_cnt;
		printk(KERN_DEBUG "end\n");
	}

	clear_bit(IS_IF_STATE_LOGGING, &this->state);

p_err:
	return count;
}

int fimc_is_hw_regdump(struct fimc_is_interface *this)
{
	int ret = 0;
	u32 i;
	void __iomem *regs;

	if (!test_bit(IS_IF_STATE_OPEN, &this->state)) {
		warn("interface is closed");
		ret = -EINVAL;
		goto p_err;
	}

	info("\n### MCUCTL Raw Dump ###\n");
	regs = (this->regs);
	for (i = 0; i < 16; ++i)
		info("MCTL Raw[%d] : %08X\n", i, readl(regs + (4 * i)));

	info("\n### COMMON REGS dump ###\n");
	regs = this->com_regs;
	for (i = 0; i < 64; ++i)
		info("MCTL[%d] : %08X\n", i, readl(regs + (4 * i)));

p_err:
	return ret;
}

int fimc_is_hw_memdump(struct fimc_is_interface *this,
	ulong start,
	ulong end)
{
	int ret = 0;
	ulong *cur;
	u32 items, offset;
	char term[50], sentence[250];

	if (!test_bit(IS_IF_STATE_OPEN, &this->state)) {
		warn("interface is closed");
		ret = -EINVAL;
		goto p_err;
	}

	cur = (ulong *)start;
	items = 0;
	offset = 0;

	memset(sentence, 0, sizeof(sentence));
	printk(KERN_DEBUG "[@] Memory Dump(0x%08lX ~ 0x%08lX)\n", start, end);

	while ((ulong)cur <= end) {
		if (!(items % 8)) {
			printk(KERN_DEBUG "%s\n", sentence);
			offset = 0;
			snprintf(term, sizeof(term), "[@] %p:      ", cur);
			snprintf(&sentence[offset], sizeof(sentence) - offset, "%s", term);
			offset += strlen(term);
		}

		snprintf(term, sizeof(term), "%016lX   ", *cur);
		snprintf(&sentence[offset], sizeof(sentence) - offset, "%s", term);
		offset += strlen(term);
		cur++;
		items++;
	}

	ret = (ulong)cur - end;

p_err:
	return ret;
}

int fimc_is_hw_enum(struct fimc_is_interface *this)
{
	int ret = 0;
	struct fimc_is_msg msg;
	volatile struct is_common_reg __iomem *com_regs;

	dbg_interface("enum()\n");

	/* check if hw_enum is already operated */
	if (test_bit(IS_IF_STATE_READY, &this->state))
		goto p_err;

	ret = wait_initstate(this);
	if (ret) {
		err("enum time out");
		ret = -ETIME;
		goto p_err;
	}

	msg.id = 0;
	msg.command = ISR_DONE;
	msg.instance = 0;
	msg.group = 0;
	msg.param1 = IHC_GET_SENSOR_NUMBER;
	/*
	 * this mean sensor numbers
	 * signel sensor: 1, dual sensor: 2, triple sensor: 3
	 */
	msg.param2 = 3;
	msg.param3 = 0;
	msg.param4 = 0;

	ret = waiting_is_ready(this);
	if (ret) {
		err("waiting for ready is fail");
	}

	com_regs = this->com_regs;
	writel(msg.command, &com_regs->hicmd);
	writel(msg.instance, &com_regs->hic_stream);
	writel(msg.param1, &com_regs->hic_param1);
	writel(msg.param2, &com_regs->hic_param2);
	writel(msg.param3, &com_regs->hic_param3);
	writel(msg.param4, &com_regs->hic_param4);
	send_interrupt(this);

	set_bit(IS_IF_STATE_READY, &this->state);

p_err:
	return ret;
}

int fimc_is_hw_fault(struct fimc_is_interface *this)
{
	int ret = 0;
	volatile struct is_common_reg __iomem *com_regs;

	dbg_interface("fault\n");

	com_regs = this->com_regs;

	enter_process_barrier(this);

	ret = waiting_is_ready(this);
	if (ret) {
		exit_process_barrier(this);
		err("waiting for ready is fail");
		ret = -EBUSY;
		goto p_err;
	}

	writel(HIC_FAULT, &com_regs->hicmd);
	writel(0, &com_regs->hic_stream);
	writel(0, &com_regs->hic_param1);
	writel(0, &com_regs->hic_param2);
	writel(0, &com_regs->hic_param3);
	writel(0, &com_regs->hic_param4);
	send_interrupt(this);

	exit_process_barrier(this);

p_err:
	return ret;
}

int fimc_is_hw_saddr(struct fimc_is_interface *this,
	u32 instance, u32 *setfile_addr)
{
	int ret = 0;
	struct fimc_is_msg msg, reply;

	dbg_interface("saddr(%d)\n", instance);

	msg.id = 0;
	msg.command = HIC_GET_SET_FILE_ADDR;
	msg.instance = instance;
	msg.group = 0;
	msg.param1 = 0;
	msg.param2 = 0;
	msg.param3 = 0;
	msg.param4 = 0;

	ret = fimc_is_set_cmd(this, &msg, &reply);
	*setfile_addr = reply.param2;

	return ret;
}

int fimc_is_hw_setfile(struct fimc_is_interface *this,
	u32 instance)
{
	int ret = 0;
	struct fimc_is_msg msg, reply;

	dbg_interface("setfile(%d)\n", instance);

	msg.id = 0;
	msg.command = HIC_LOAD_SET_FILE;
	msg.instance = instance;
	msg.group = 0;
	msg.param1 = 0;
	msg.param2 = 0;
	msg.param3 = 0;
	msg.param4 = 0;

	ret = fimc_is_set_cmd(this, &msg, &reply);

	return ret;
}

int fimc_is_hw_open(struct fimc_is_interface *this,
	u32 instance,
	u32 module_id,
	u32 info,
	u32 path,
	u32 flag,
	u32 *mwidth,
	u32 *mheight)
{
	int ret;
	struct fimc_is_msg msg, reply;

	dbg_interface("open(%d,%08X)\n", module_id, flag);

	msg.id = 0;
	msg.command = HIC_OPEN_SENSOR;
	msg.instance = instance;
	msg.group = 0;
	msg.param1 = module_id;
	msg.param2 = info;
	msg.param3 = path;
	msg.param4 = flag;

	ret = fimc_is_set_cmd(this, &msg, &reply);

	*mwidth = reply.param2;
	*mheight = reply.param3;

	return ret;
}

int fimc_is_hw_close(struct fimc_is_interface *this,
	u32 instance)
{
	int ret;
	struct fimc_is_msg msg, reply;

	dbg_interface("close sensor(instance:%d)\n", instance);

	msg.id = 0;
	msg.command = HIC_CLOSE_SENSOR;
	msg.instance = instance;
	msg.group = 0;
	msg.param1 = 0;
	msg.param2 = 0;
	msg.param3 = 0;
	msg.param4 = 0;

	ret = fimc_is_set_cmd(this, &msg, &reply);

	return ret;
}

int fimc_is_hw_stream_on(struct fimc_is_interface *this,
	u32 instance)
{
	int ret;
	struct fimc_is_msg msg, reply;

	BUG_ON(!this);

	dbg_interface("stream_on(%d)\n", instance);

	msg.id = 0;
	msg.command = HIC_STREAM_ON;
	msg.instance = instance;
	msg.group = 0;
	msg.param1 = 0;
	msg.param2 = 0;
	msg.param3 = 0;
	msg.param4 = 0;

	ret = fimc_is_set_cmd(this, &msg, &reply);

	return ret;
}

int fimc_is_hw_stream_off(struct fimc_is_interface *this,
	u32 instance)
{
	int ret;
	struct fimc_is_msg msg, reply;

	dbg_interface("stream_off(%d)\n", instance);

	msg.id = 0;
	msg.command = HIC_STREAM_OFF;
	msg.instance = instance;
	msg.group = 0;
	msg.param1 = 0;
	msg.param2 = 0;
	msg.param3 = 0;
	msg.param4 = 0;

	ret = fimc_is_set_cmd(this, &msg, &reply);

	return ret;
}

int fimc_is_hw_process_on(struct fimc_is_interface *this,
	u32 instance, u32 group)
{
	int ret;
	struct fimc_is_msg msg, reply;

	dbg_interface("process_on(%d)\n", instance);

	msg.id = 0;
	msg.command = HIC_PROCESS_START;
	msg.instance = instance;
	msg.group = group;
	msg.param1 = 0;
	msg.param2 = 0;
	msg.param3 = 0;
	msg.param4 = 0;

	ret = fimc_is_set_cmd(this, &msg, &reply);

	return ret;
}

int fimc_is_hw_process_off(struct fimc_is_interface *this,
	u32 instance, u32 group, u32 mode)
{
	int ret;
	struct fimc_is_msg msg, reply;

	WARN_ON(mode >= 2);

	dbg_interface("process_off(%d)\n", instance);

	msg.id = 0;
	msg.command = HIC_PROCESS_STOP;
	msg.instance = instance;
	msg.group = group;
	msg.param1 = mode;
	msg.param2 = 0;
	msg.param3 = 0;
	msg.param4 = 0;

	ret = fimc_is_set_cmd(this, &msg, &reply);

	return ret;
}

int fimc_is_hw_i2c_lock(struct fimc_is_interface *this,
	u32 instance, int i2c_clk, bool lock)
{
	int ret;
	struct fimc_is_msg msg, reply;

	dbg_interface("i2c_lock(%d)\n", instance);

	msg.id = 0;
	msg.command = HIC_I2C_CONTROL_LOCK;
	msg.instance = instance;
	msg.group = 0;
	msg.param1 = lock;
	msg.param2 = i2c_clk;
	msg.param3 = 0;
	msg.param4 = 0;

	ret = fimc_is_set_cmd(this, &msg, &reply);

	return ret;
}

int fimc_is_hw_s_param(struct fimc_is_interface *this,
	u32 instance, u32 lindex, u32 hindex, u32 indexes)
{
	int ret;
	struct fimc_is_msg msg, reply;

	dbg_interface("s_param(%d)\n", instance);

	msg.id = 0;
	msg.command = HIC_SET_PARAMETER;
	msg.instance = instance;
	msg.group = 0;
	msg.param1 = ISS_PREVIEW_STILL;
	msg.param2 = indexes;
	msg.param3 = lindex;
	msg.param4 = hindex;

	ret = fimc_is_set_cmd(this, &msg, &reply);

	return ret;
}

int fimc_is_hw_a_param(struct fimc_is_interface *this,
	u32 instance, u32 group, u32 sub_mode)
{
	int ret = 0;
	struct fimc_is_msg msg, reply;

	dbg_interface("a_param(%d)\n", instance);

	msg.id = 0;
	msg.command = HIC_PREVIEW_STILL;
	msg.instance = instance;
	msg.group = group;
	msg.param1 = sub_mode;
	msg.param2 = 0;
	msg.param3 = 0;
	msg.param4 = 0;

	ret = fimc_is_set_cmd(this, &msg, &reply);

	return ret;
}

int fimc_is_hw_g_capability(struct fimc_is_interface *this,
	u32 instance, u32 address)
{
	int ret;
	struct fimc_is_msg msg, reply;

	dbg_interface("g_capability(%d)\n", instance);

	msg.id = 0;
	msg.command = HIC_GET_STATIC_METADATA;
	msg.instance = instance;
	msg.group = 0;
	msg.param1 = address;
	msg.param2 = 0;
	msg.param3 = 0;
	msg.param4 = 0;

	ret = fimc_is_set_cmd(this, &msg, &reply);

	return ret;
}

int fimc_is_hw_map(struct fimc_is_interface *this,
	u32 instance, u32 group, u32 address, u32 size)
{
	int ret;
	struct fimc_is_msg msg, reply;

	dbg_interface("[%d][%d] map(%08X, %X)\n", instance, group, address, size);

	msg.id = 0;
	msg.command = HIC_SET_A5_MAP;
	msg.instance = instance;
	msg.group = group;
	msg.param1 = address;
	msg.param2 = size;
	msg.param3 = 0;
	msg.param4 = 0;

	ret = fimc_is_set_cmd(this, &msg, &reply);

	return ret;
}

int fimc_is_hw_unmap(struct fimc_is_interface *this,
	u32 instance, u32 group)
{
	int ret;
	struct fimc_is_msg msg, reply;

	dbg_interface("[%d][%d] unmap\n", instance, group);

	msg.id = 0;
	msg.command = HIC_SET_A5_UNMAP;
	msg.instance = instance;
	msg.group = group;
	msg.param1 = 0;
	msg.param2 = 0;
	msg.param3 = 0;
	msg.param4 = 0;

	ret = fimc_is_set_cmd(this, &msg, &reply);

	return ret;
}

int fimc_is_hw_power_down(struct fimc_is_interface *this,
	u32 instance)
{
	int ret = 0;
	struct fimc_is_msg msg, reply;

	dbg_interface("pwr_down(%d)\n", instance);

	if (!test_bit(IS_IF_STATE_START, &this->state)) {
		warn("instance(%d): FW is not initialized, wait\n", instance);
		ret = fimc_is_hw_enum(this);
		if (ret)
			err("fimc_is_itf_enum is fail(%d)", ret);
	}

	msg.id = 0;
	msg.command = HIC_POWER_DOWN;
	msg.instance = instance;
	msg.group = 0;
	msg.param1 = 0;
	msg.param2 = 0;
	msg.param3 = 0;
	msg.param4 = 0;

	ret = fimc_is_set_cmd(this, &msg, &reply);

	return ret;
}

#if (FW_HAS_SYS_CTRL_CMD)
int fimc_is_hw_sys_ctl(struct fimc_is_interface *this,
	u32 instance, int cmd, int val)
{
	int ret;
	struct fimc_is_msg msg, reply;

	dbg_interface("sys_ctl(cmd(%d), val(%d))\n", cmd, val);

	msg.id = 0;
	msg.command = HIC_SYSTEM_CONTROL;
	msg.instance = instance;
	msg.group = 0;
	msg.param1 = cmd;
	msg.param2 = val;
	msg.param3 = 0;
	msg.param4 = 0;

	ret = fimc_is_set_cmd(this, &msg, &reply);

	return ret;
}
#else
int fimc_is_hw_sys_ctl(struct fimc_is_interface *this,
	u32 instance, int cmd, int val)
{
	return 0;
}
#endif

#if (FW_HAS_SENSOR_MODE_CMD)
int fimc_is_hw_sensor_mode(struct fimc_is_interface *this,
	u32 instance, int cfg)
{
	int ret;
	struct fimc_is_msg msg, reply;

	dbg_interface("sensor mode(%d): %d\n", instance, cfg);

	msg.id = 0;
	msg.command = HIC_SENSOR_MODE_CHANGE;
	msg.instance = instance;
	msg.group = 0;
	msg.param1 = cfg;
	msg.param2 = 0;
	msg.param3 = 0;
	msg.param4 = 0;

	ret = fimc_is_set_cmd(this, &msg, &reply);

	return ret;
}
#else
int fimc_is_hw_sensor_mode(struct fimc_is_interface *this,
	u32 instance, int cfg)
{
	return 0;
}
#endif

int fimc_is_hw_shot_nblk(struct fimc_is_interface *this,
#if (HOST_FW_INTERFACE_VER >= 2)
	u32 instance, u32 group, u32 shot, u32 fcount, u32 rcount
#else
        u32 instance, u32 group, u32 bayer, u32 shot, u32 fcount, u32 rcount
#endif
	)
{
	int ret = 0;
	struct fimc_is_msg msg;

	/*dbg_interface("shot_nblk(%d, %d)\n", instance, fcount);*/

	msg.id = 0;
	msg.command = HIC_SHOT;
	msg.instance = instance;
	msg.group = group;
#if (HOST_FW_INTERFACE_VER >= 2)
	msg.param1 = shot;
	msg.param2 = fcount;
	msg.param3 = rcount;
#else
	msg.param1 = bayer;
	msg.param2 = shot;
	msg.param3 = fcount;
	msg.param4 = rcount;
#endif

	ret = fimc_is_set_cmd_shot(this, &msg);

	return ret;
}

int fimc_is_hw_s_camctrl_nblk(struct fimc_is_interface *this,
	u32 instance, u32 address, u32 fcount)
{
	int ret = 0;
	struct fimc_is_work *work;
	struct fimc_is_msg *msg;

	dbg_interface("cam_ctrl_nblk(%d)\n", instance);

	get_free_work(&this->nblk_cam_ctrl, &work);

	if (work) {
		work->fcount = fcount;
		msg = &work->msg;
		msg->id = 0;
		msg->command = HIC_SET_CAM_CONTROL;
		msg->instance = instance;
		msg->group = 0;
		msg->param1 = address;
		msg->param2 = fcount;
		msg->param3 = 0;
		msg->param4 = 0;

		ret = fimc_is_set_cmd_nblk(this, work);
	} else {
		err("g_free_nblk return NULL");
		print_fre_work_list(&this->nblk_cam_ctrl);
		print_req_work_list(&this->nblk_cam_ctrl);
		ret = 1;
	}

	return ret;
}

int fimc_is_hw_msg_test(struct fimc_is_interface *this, u32 sync_id, u32 msg_test_id)
{
	int ret = 0;
	struct fimc_is_work work;
	struct fimc_is_msg *msg;

	dbg_interface("msg_test_nblk(%d)\n", msg_test_id);

	msg = &work.msg;
	msg->id = 0;
	msg->command = HIC_MSG_TEST;
	msg->instance = 0;
	msg->group = 0;
	msg->param1 = msg_test_id;
	msg->param2 = sync_id;
	msg->param3 = 0;
	msg->param4 = 0;

	ret = fimc_is_set_cmd_nblk(this, &work);

	return ret;
}
