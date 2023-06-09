/* linux/drivers/video/mdnie.c
 *
 * Register interface file for Samsung mDNIe driver
 *
*/

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/mutex.h>
#include <linux/mm.h>
#include <linux/device.h>
#include <linux/backlight.h>
#include <linux/platform_device.h>
#include <linux/delay.h>
#include <linux/lcd.h>
#include <linux/fb.h>
#include <linux/pm_runtime.h>

#include "mdnie.h"

#if defined(CONFIG_DECON_LCD_S6E3FA2)
#include "mdnie_lite_table_k.h"
#elif defined(CONFIG_DECON_LCD_S6E3HA0)
#include "mdnie_lite_table_kq.h"
#elif defined(CONFIG_DECON_LCD_EA8064G)
#include "mdnie_lite_table_s.h"
#elif defined(CONFIG_DECON_LCD_S6E3HA2)
#include "mdnie_lite_table_tr.h"
#elif defined(CONFIG_DECON_LCD_S6E3HF2)
#include "mdnie_lite_table_tb.h"
#elif defined(CONFIG_PANEL_S6E3HF2_DYNAMIC)
#include "mdnie_lite_table_zero.h"
#elif defined(CONFIG_PANEL_S6E3HA2_DYNAMIC)
#include "mdnie_lite_table_zerof.h"
#elif defined(CONFIG_PANEL_S6E3HA3_DYNAMIC)
#include "mdnie_lite_table_noble.h"
#elif defined(CONFIG_PANEL_S6E3HF3_DYNAMIC)
#include "mdnie_lite_table_zen.h"
#elif defined(CONFIG_PANEL_EA8061V_XGA)
#include "mdnie_lite_table_royce.h"
#endif
#if defined(CONFIG_TDMB)
#include "mdnie_lite_table_dmb.h"
#endif

#define MDNIE_SYSFS_PREFIX		"/sdcard/mdnie/"
#define PANEL_COORDINATE_PATH	"/sys/class/lcd/panel/color_coordinate"

#define IS_DMB(idx)			(idx == DMB_NORMAL_MODE)
#define IS_SCENARIO(idx)		((idx < SCENARIO_MAX) && !((idx > VIDEO_NORMAL_MODE) && (idx < CAMERA_MODE)))
#define IS_ACCESSIBILITY(idx)		(idx && (idx < ACCESSIBILITY_MAX))
#if defined(CONFIG_PANEL_S6E3HA3_DYNAMIC) || defined(CONFIG_PANEL_S6E3HF3_DYNAMIC)
#define IS_HBM(idx)			(idx >= AOLCE_MIN_BRIGHTNESS)
#else
#define IS_HBM(idx)			(idx >= 6)
#endif
#if defined(CONFIG_LCD_HMT)
#define IS_HMT(idx)			(idx >= HMT_MDNIE_ON && idx < HMT_MDNIE_MAX)
#endif

#define SCENARIO_IS_VALID(idx)	(IS_DMB(idx) || IS_SCENARIO(idx))

/* Split 16 bit as 8bit x 2 */
#define GET_MSB_8BIT(x)		((x >> 8) & (BIT(8) - 1))
#define GET_LSB_8BIT(x)		((x >> 0) & (BIT(8) - 1))

static struct class *mdnie_class;

/* Do not call mdnie write directly */
static int mdnie_write(struct mdnie_info *mdnie, struct mdnie_table *table)
{
	int ret = 0;

	if (mdnie->enable)
		ret = mdnie->ops.write(mdnie->data, table->tune, MDNIE_CMD_MAX);

	return ret;
}

static int mdnie_write_table(struct mdnie_info *mdnie, struct mdnie_table *table)
{
	int i, ret = 0;
	struct mdnie_table *buf = NULL;

	for (i = 0; i < MDNIE_CMD_MAX; i++) {
		if (IS_ERR_OR_NULL(table->tune[i].sequence)) {
			dev_err(mdnie->dev, "mdnie sequence %s is null, %lx\n",
					table->name, (unsigned long)table->tune[i].sequence);
			return -EPERM;
		}
	}

	mutex_lock(&mdnie->dev_lock);

	buf = table;

	ret = mdnie_write(mdnie, buf);

	mutex_unlock(&mdnie->dev_lock);

	return ret;
}

static struct mdnie_table *mdnie_find_table(struct mdnie_info *mdnie)
{
	struct mdnie_table *table = NULL;

	mutex_lock(&mdnie->lock);

	if (IS_ACCESSIBILITY(mdnie->accessibility)) {
		table = &accessibility_table[mdnie->accessibility];
		goto exit;
#ifdef CONFIG_LCD_HMT
	} else if (IS_HMT(mdnie->hmt_mode)) {
		table = &hmt_table[mdnie->hmt_mode];
		goto exit;
#endif
	} else if (IS_HBM(mdnie->auto_brightness)) {
#if defined(CONFIG_PANEL_S6E3HA3_DYNAMIC) || defined(CONFIG_PANEL_S6E3HF3_DYNAMIC)
		if(mdnie->auto_brightness == AOLCE_MIN_BRIGHTNESS)
			table = &hbm_table[HBM_AOLCE_1];
		else if(mdnie->auto_brightness >= AOLCE_MAX_BRIGHTNESS)
			table = &hbm_table[HBM_AOLCE_3];
		else
			table = &hbm_table[HBM_AOLCE_2];
#else
		if((mdnie->scenario == BROWSER_MODE) || (mdnie->scenario == EBOOK_MODE))
			table = &hbm_table[HBM_ON_TEXT];
		else
			table = &hbm_table[HBM_ON];
#endif
		goto exit;
#if defined(CONFIG_TDMB)
	} else if (IS_DMB(mdnie->scenario)) {
		table = &dmb_table[mdnie->mode];
		goto exit;
#endif
	} else if (IS_SCENARIO(mdnie->scenario)) {
		table = &tuning_table[mdnie->scenario][mdnie->mode];
		goto exit;
	}

exit:
#if defined(CONFIG_PANEL_S6E3HA3_DYNAMIC) || defined(CONFIG_PANEL_S6E3HF3_DYNAMIC)
	if((mdnie->disable_trans_dimming) && (table != NULL)) {
		dev_info(mdnie->dev, "%s: disable_trans_dimming=%d\n", __func__, mdnie->disable_trans_dimming);
		memcpy(&(mdnie->table_buffer), table, sizeof(struct mdnie_table));
		memcpy(mdnie->sequence_buffer, table->tune[MDNIE_CMD1].sequence,
				table->tune[MDNIE_CMD1].size);
		mdnie->table_buffer.tune[MDNIE_CMD1].sequence = mdnie->sequence_buffer;
		mdnie->table_buffer.tune[MDNIE_CMD1].sequence
					[MDNIE_TRANS_DIMMING_OFFSET] = 0x0;

		mutex_unlock(&mdnie->lock);
		return &(mdnie->table_buffer);
	}
#endif
	mutex_unlock(&mdnie->lock);

	return table;
}

static void mdnie_update_sequence(struct mdnie_info *mdnie, struct mdnie_table *table)
{
	struct mdnie_table *t = NULL;

	if (mdnie->tuning) {
		t = mdnie_request_table(mdnie->path, table);
		if (!IS_ERR_OR_NULL(t) && !IS_ERR_OR_NULL(t->name))
			mdnie_write_table(mdnie, t);
		else
			mdnie_write_table(mdnie, table);
	} else
		mdnie_write_table(mdnie, table);
	return;
}

static void mdnie_update(struct mdnie_info *mdnie)
{
	struct mdnie_table *table = NULL;

	if (!mdnie->enable) {
		dev_err(mdnie->dev, "mdnie state is off\n");
		return;
	}

	table = mdnie_find_table(mdnie);
	if (!IS_ERR_OR_NULL(table) && !IS_ERR_OR_NULL(table->name)) {
		mdnie_update_sequence(mdnie, table);
		dev_info(mdnie->dev, "%s\n", table->name);

		mdnie->white_r = table->tune[MDNIE_CMD1].sequence[MDNIE_WHITE_R];
		mdnie->white_g = table->tune[MDNIE_CMD1].sequence[MDNIE_WHITE_G];
		mdnie->white_b = table->tune[MDNIE_CMD1].sequence[MDNIE_WHITE_B];
	}

	return;
}

static void update_color_position(struct mdnie_info *mdnie, unsigned int idx)
{
	u8 mode, scenario;
	mdnie_t *wbuf;

	dev_info(mdnie->dev, "%s: idx=%d\n", __func__, idx);

	mutex_lock(&mdnie->lock);

	for (mode = 0; mode < MODE_MAX; mode++) {
		for (scenario = 0; scenario <= EMAIL_MODE; scenario++) {
			wbuf = tuning_table[scenario][mode].tune[MDNIE_CMD1].sequence;
			if (IS_ERR_OR_NULL(wbuf))
				continue;
			if (scenario != EBOOK_MODE) {
				wbuf[MDNIE_WHITE_R] = coordinate_data[mode][idx][0];
				wbuf[MDNIE_WHITE_G] = coordinate_data[mode][idx][1];
				wbuf[MDNIE_WHITE_B] = coordinate_data[mode][idx][2];
			}
		}
	}

	mutex_unlock(&mdnie->lock);
}

static int get_panel_coordinate(struct mdnie_info *mdnie, int *result)
{
	int ret = 0;
	int pi = get_panel_index_init();

	unsigned short x, y;

	x = mdnie->coordinate[pi][0];
	y = mdnie->coordinate[pi][1];


	result[1] = COLOR_OFFSET_F1(x, y);
	result[2] = COLOR_OFFSET_F2(x, y);
	result[3] = COLOR_OFFSET_F3(x, y);
	result[4] = COLOR_OFFSET_F4(x, y);

	ret = mdnie_calibration(result);
	dev_info(mdnie->dev, "%s: %d, %d, idx=%d\n", __func__, x, y, ret);

//skip_color_correction:
	mdnie->color_correction = 1;

	return ret;
}

#ifdef CONFIG_FOLDER_DUAL_PANEL
/* void mdnie_update_directly(void * mdnie)
* case 1: called when waking up before fb_notifier_callback runs
*            mdnie->enable is 0 and return;
* case 2: called when panel flipping, it can run and set mdnie.
*/
void mdnie_update_directly(void * mdnie)
{
	static struct mdnie_info *p_mdnie = NULL;

	pr_debug("%s : +\n", __func__);
	if ((!mdnie) && (p_mdnie))
		mdnie_update(p_mdnie);
	else
		p_mdnie = (struct mdnie_info *) mdnie;
	pr_debug("%s : -\n", __func__);
}
EXPORT_SYMBOL(mdnie_update_directly);
#endif

static ssize_t mode_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct mdnie_info *mdnie = dev_get_drvdata(dev);

	return sprintf(buf, "%d\n", mdnie->mode);
}

static ssize_t mode_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count)
{
	struct mdnie_info *mdnie = dev_get_drvdata(dev);
	unsigned int value = 0;
	int ret;
	int result[5] = {0,};

	ret = kstrtouint(buf, 0, &value);
	if (ret < 0)
		return ret;

	dev_info(dev, "%s: value=%d\n", __func__, value);

	if (value >= MODE_MAX) {
		value = STANDARD;
		return -EINVAL;
	}

	mutex_lock(&mdnie->lock);
	mdnie->mode = value;
	mutex_unlock(&mdnie->lock);

	if (!mdnie->color_correction) {
		ret = get_panel_coordinate(mdnie, result);
		if (ret > 0)
			update_color_position(mdnie, ret);
	}

	mdnie_update(mdnie);

	return count;
}


static ssize_t scenario_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct mdnie_info *mdnie = dev_get_drvdata(dev);

	return sprintf(buf, "%d\n", mdnie->scenario);
}

static ssize_t scenario_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count)
{
	struct mdnie_info *mdnie = dev_get_drvdata(dev);
	unsigned int value;
	int ret;

	ret = kstrtouint(buf, 0, &value);
	if (ret < 0)
		return ret;

	dev_info(dev, "%s: value=%d\n", __func__, value);

	if (!SCENARIO_IS_VALID(value))
		value = UI_MODE;

	mutex_lock(&mdnie->lock);
	mdnie->scenario = value;
	mutex_unlock(&mdnie->lock);

	mdnie_update(mdnie);

	return count;
}

static ssize_t tuning_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct mdnie_info *mdnie = dev_get_drvdata(dev);
	char *pos = buf;
	struct mdnie_table *table = NULL;
	int i;

	pos += sprintf(pos, "++ %s: %s\n", __func__, mdnie->path);

	if (!mdnie->tuning) {
		pos += sprintf(pos, "tunning mode is off\n");
		goto exit;
	}

	if (strncmp(mdnie->path, MDNIE_SYSFS_PREFIX, sizeof(MDNIE_SYSFS_PREFIX) - 1)) {
		pos += sprintf(pos, "file path is invalid, %s\n", mdnie->path);
		goto exit;
	}

	table = mdnie_find_table(mdnie);
	if (!IS_ERR_OR_NULL(table) && !IS_ERR_OR_NULL(table->name)) {
		table = mdnie_request_table(mdnie->path, table);
		for (i = 0; i < table->tune[MDNIE_CMD1].size; i++)
			pos += sprintf(pos, "0x%02x ", table->tune[MDNIE_CMD1].sequence[i]);
		pos += sprintf(pos, "\n");
		if (MDNIE_CMD1 != MDNIE_CMD2) {
			for (i = 0; i < table->tune[MDNIE_CMD2].size; i++)
				pos += sprintf(pos, "0x%02x ", table->tune[MDNIE_CMD2].sequence[i]);
		}
#if defined(CONFIG_PANEL_S6E3HA3_DYNAMIC) || defined(CONFIG_PANEL_S6E3HF3_DYNAMIC)
		pos += sprintf(pos, "\n");
		if(table->tune[MDNIE_CMD3].sequence != NULL) {
			for (i = 0; i < table->tune[MDNIE_CMD3].size; i++)
				pos += sprintf(pos, "0x%02x ", table->tune[MDNIE_CMD3].sequence[i]);
		}
#endif
	}

exit:
	pos += sprintf(pos, "-- %s\n", __func__);

	return pos - buf;
}

static ssize_t tuning_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count)
{
	struct mdnie_info *mdnie = dev_get_drvdata(dev);
	int ret;

	if (sysfs_streq(buf, "0") || sysfs_streq(buf, "1")) {
		ret = kstrtouint(buf, 0, &mdnie->tuning);
		if (ret < 0)
			return ret;
		if (!mdnie->tuning)
			memset(mdnie->path, 0, sizeof(mdnie->path));

		dev_info(dev, "%s: %s\n", __func__, mdnie->tuning ? "enable" : "disable");
	} else {
		if (!mdnie->tuning)
			return count;

		if (count > (sizeof(mdnie->path) - sizeof(MDNIE_SYSFS_PREFIX))) {
			dev_err(dev, "file name %s is too long\n", mdnie->path);
			return -ENOMEM;
		}

		memset(mdnie->path, 0, sizeof(mdnie->path));
		snprintf(mdnie->path, sizeof(MDNIE_SYSFS_PREFIX) + count-1, "%s%s", MDNIE_SYSFS_PREFIX, buf);
		dev_info(dev, "%s: %s\n", __func__, mdnie->path);

		mdnie_update(mdnie);
	}

	return count;
}

static ssize_t accessibility_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct mdnie_info *mdnie = dev_get_drvdata(dev);

	return sprintf(buf, "%d\n", mdnie->accessibility);
}

static ssize_t accessibility_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count)
{
	struct mdnie_info *mdnie = dev_get_drvdata(dev);
	int value;
	unsigned int s[12] = {0, }, i = 0;
	int ret;
	mdnie_t *wbuf;

	ret = sscanf(buf, "%d %x %x %x %x %x %x %x %x %x %x %x %x",
		&value, &s[0], &s[1], &s[2], &s[3],
		&s[4], &s[5], &s[6], &s[7], &s[8], &s[9], &s[10], &s[11]);

	dev_info(dev, "%s: value=%d param cnt : %d\n", __func__, value, ret);

	if (ret < 0)
		return ret;
	else {
		if (value >= ACCESSIBILITY_MAX)
			value = ACCESSIBILITY_OFF;

		mutex_lock(&mdnie->lock);
		mdnie->accessibility = value;
		if (value == COLOR_BLIND) {
			if (ret != MDNIE_COLOR_BLIND_TUNE_CNT + 1) {
				mutex_unlock(&mdnie->lock);
				return -EINVAL;
			}
			wbuf = &accessibility_table[COLOR_BLIND].tune[MDNIE_CMD1].sequence[MDNIE_COLOR_BLIND_OFFSET];
			while (i < MDNIE_COLOR_BLIND_TUNE_CNT) {
				wbuf[i * 2 + 0] = GET_LSB_8BIT(s[i]);
				wbuf[i * 2 + 1] = GET_MSB_8BIT(s[i]);
				i++;
			}
			dev_info(dev, "%s: %s\n", __func__, buf);
		} else if (value == COLOR_BLIND_HBM) {
			if (ret != MDNIE_COLOR_BLIND_HBM_TUNE_CNT + 1) {
				mutex_unlock(&mdnie->lock);
				return -EINVAL;
			}
			wbuf = &accessibility_table[COLOR_BLIND_HBM].tune[MDNIE_CMD1].sequence[MDNIE_COLOR_BLIND_OFFSET];
			while (i < MDNIE_COLOR_BLIND_HBM_TUNE_CNT) {
				wbuf[i * 2 + 0] = GET_LSB_8BIT(s[i]);
				wbuf[i * 2 + 1] = GET_MSB_8BIT(s[i]);
				i++;
			}
			dev_info(dev, "%s: %s\n", __func__, buf);
		}
		mutex_unlock(&mdnie->lock);

		mdnie_update(mdnie);
	}

	return count;
}

static ssize_t color_correct_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct mdnie_info *mdnie = dev_get_drvdata(dev);
	char *pos = buf;
	int i, idx, result[5] = {0,};

	if (!mdnie->color_correction)
		return -EINVAL;

	idx = get_panel_coordinate(mdnie, result);

	for (i = 1; i < (int) ARRAY_SIZE(result); i++)
		pos += sprintf(pos, "f%d: %d, ", i, result[i]);
	pos += sprintf(pos, "tune%d\n", idx);

	return pos - buf;
}

static ssize_t bypass_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct mdnie_info *mdnie = dev_get_drvdata(dev);

	return sprintf(buf, "%d\n", mdnie->bypass);
}

static ssize_t bypass_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count)
{
	struct mdnie_info *mdnie = dev_get_drvdata(dev);
	struct mdnie_table *table = NULL;
	unsigned int value;
	int ret;

	ret = kstrtouint(buf, 0, &value);

	dev_info(dev, "%s :: value=%d\n", __func__, value);

	if (ret < 0)
		return ret;
	else {
		if (value >= BYPASS_MAX)
			value = BYPASS_OFF;

		value = (value) ? BYPASS_ON : BYPASS_OFF;

		mutex_lock(&mdnie->lock);
		mdnie->bypass = value;
		mutex_unlock(&mdnie->lock);

		table = &bypass_table[value];
		if (!IS_ERR_OR_NULL(table)) {
			mdnie_write_table(mdnie, table);
			dev_info(mdnie->dev, "%s\n", table->name);
		}
	}

	return count;
}

static ssize_t auto_brightness_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
#if defined(CONFIG_PANEL_S6E3HA3_DYNAMIC) || defined(CONFIG_PANEL_S6E3HF3_DYNAMIC)
	return sprintf(buf, "%d\n", AOLCE_MAX_BRIGHTNESS);
#else
	struct mdnie_info *mdnie = dev_get_drvdata(dev);

	return sprintf(buf, "%d, hbm: %d\n", mdnie->auto_brightness, mdnie->hbm);
#endif
}

static ssize_t auto_brightness_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count)
{
	struct mdnie_info *mdnie = dev_get_drvdata(dev);
	unsigned int value;
	int ret;
	static unsigned int update;

	ret = kstrtouint(buf, 0, &value);
	if (ret < 0)
		return ret;

	dev_info(dev, "%s: value=%d\n", __func__, value);

	mutex_lock(&mdnie->lock);
#if defined(CONFIG_PANEL_S6E3HA3_DYNAMIC) || defined(CONFIG_PANEL_S6E3HF3_DYNAMIC)
	update = 0;
	if(IS_HBM(mdnie->auto_brightness) == IS_HBM(value)){
		if(IS_HBM(mdnie->auto_brightness)&&(mdnie->auto_brightness != value))
			update = 1;
	}
	else
		update = 1;
#else
	update = (IS_HBM(mdnie->auto_brightness) != IS_HBM(value)) ? 1 : 0;
#endif
	mdnie->hbm = IS_HBM(value) ? HBM_ON : HBM_OFF;
	mdnie->auto_brightness = value;
	mutex_unlock(&mdnie->lock);

	if (update)
		mdnie_update(mdnie);

	return count;
}

/* Temporary solution: Do not use this sysfs as official purpose */
static ssize_t mdnie_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct mdnie_info *mdnie = dev_get_drvdata(dev);
	char *pos = buf;
	struct mdnie_table *table = NULL;
	int i, j;
	u8 *buffer;

	if (!mdnie->enable) {
		dev_err(mdnie->dev, "mdnie state is off\n");
		goto exit;
	}

	table = mdnie_find_table(mdnie);

	for (i = 0; i < MDNIE_CMD_MAX; i++) {
		if (IS_ERR_OR_NULL(table->tune[i].sequence)) {
			dev_err(mdnie->dev, "mdnie sequence %s is null, %lx\n",
				table->name, (unsigned long)table->tune[i].sequence);
			goto exit;
		}
	}

	/* should be fixed later, or removed */
	/* mdnie->ops.write(mdnie->data, table->tune[LEVEL1_KEY_UNLOCK].sequence, table->tune[LEVEL1_KEY_UNLOCK].size); */

	pos += sprintf(pos, "+ %s\n", table->name);

	for (j = MDNIE_CMD1; j <= MDNIE_CMD2; j++) {
		buffer = kzalloc(table->tune[j].size, GFP_KERNEL);

		mdnie->ops.read(mdnie->data, table->tune[j].sequence[0], buffer, table->tune[j].size - 1);

		for (i = 0; i < table->tune[j].size - 1; i++) {
			pos += sprintf(pos, "%3d:\t0x%02x\t0x%02x", i + 1, table->tune[j].sequence[i+1], buffer[i]);
			if (table->tune[j].sequence[i+1] != buffer[i])
				pos += sprintf(pos, "\t(X)");
			pos += sprintf(pos, "\n");
		}

		kfree(buffer);
	}

	pos += sprintf(pos, "- %s\n", table->name);

	/* mdnie->ops.write(mdnie->data, table->tune[LEVEL1_KEY_LOCK].sequence, table->tune[LEVEL1_KEY_LOCK].size); */

exit:
	return pos - buf;
}

static ssize_t sensorRGB_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct mdnie_info *mdnie = dev_get_drvdata(dev);
	return sprintf(buf, "%d %d %d\n", mdnie->white_r,
		mdnie->white_g, mdnie->white_b);
}

static ssize_t sensorRGB_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count)
{
	struct mdnie_info *mdnie = dev_get_drvdata(dev);
	struct mdnie_table *table = NULL;
	unsigned int white_red, white_green, white_blue;
	int ret;

	ret = sscanf(buf, "%d %d %d"
		, &white_red, &white_green, &white_blue);
	if (ret < 0)
		return ret;

	if (mdnie->enable && (mdnie->accessibility == ACCESSIBILITY_OFF)
		&& (mdnie->mode == AUTO)
		&& ((mdnie->scenario == BROWSER_MODE)
		|| (mdnie->scenario == EBOOK_MODE))) {
		dev_info(dev, "%s, white_r %d, white_g %d, white_b %d\n",
			__func__, white_red, white_green, white_blue);

		table = mdnie_find_table(mdnie);

		memcpy(&(mdnie->table_buffer),
			table, sizeof(struct mdnie_table));
		memcpy(mdnie->sequence_buffer,
			table->tune[MDNIE_CMD1].sequence,
			table->tune[MDNIE_CMD1].size);
		mdnie->table_buffer.tune[MDNIE_CMD1].sequence
			= mdnie->sequence_buffer;

		mdnie->table_buffer.tune[MDNIE_CMD1].sequence
			[MDNIE_WHITE_R] = (unsigned char)white_red;
		mdnie->table_buffer.tune[MDNIE_CMD1].sequence
			[MDNIE_WHITE_G] = (unsigned char)white_green;
		mdnie->table_buffer.tune[MDNIE_CMD1].sequence
			[MDNIE_WHITE_B] = (unsigned char)white_blue;

		mdnie->white_r = white_red;
		mdnie->white_g = white_green;
		mdnie->white_b = white_blue;

		mdnie_update_sequence(mdnie, &(mdnie->table_buffer));
	}

	return count;
}
#ifdef CONFIG_LCD_HMT
static ssize_t hmtColorTemp_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct mdnie_info *mdnie = dev_get_drvdata(dev);

	return sprintf(buf, "hmt_mode: %d\n", mdnie->hmt_mode);
}

static ssize_t hmtColorTemp_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count)
{
	struct mdnie_info *mdnie = dev_get_drvdata(dev);
	unsigned int value;
	int ret;

	ret = kstrtouint(buf, 0, &value);
	if (ret < 0)
		return ret;


	if (value != mdnie->hmt_mode && value < HMT_MDNIE_MAX) {
		mutex_lock(&mdnie->lock);
		mdnie->hmt_mode = value;
		mutex_unlock(&mdnie->lock);
		mdnie_update(mdnie);
	}

	return count;
}
#endif

static struct device_attribute mdnie_attributes[] = {
	__ATTR(mode, 0664, mode_show, mode_store),
	__ATTR(scenario, 0664, scenario_show, scenario_store),
	__ATTR(tuning, 0664, tuning_show, tuning_store),
	__ATTR(accessibility, 0664, accessibility_show, accessibility_store),
	__ATTR(color_correct, 0444, color_correct_show, NULL),
	__ATTR(bypass, 0664, bypass_show, bypass_store),
	__ATTR(auto_brightness, 0664, auto_brightness_show, auto_brightness_store),
	__ATTR(mdnie, 0444, mdnie_show, NULL),
	__ATTR(sensorRGB, 0664, sensorRGB_show, sensorRGB_store),
#ifdef CONFIG_LCD_HMT
	__ATTR(hmt_color_temperature, 0664, hmtColorTemp_show, hmtColorTemp_store),
#endif
	__ATTR_NULL,
};

static int fb_notifier_callback(struct notifier_block *self,
				 unsigned long event, void *data)
{
	struct mdnie_info *mdnie;
	struct fb_event *evdata = data;
	int fb_blank;

	switch (event) {
	case FB_EVENT_BLANK:
		break;
	default:
		return 0;
	}

	mdnie = container_of(self, struct mdnie_info, fb_notif);

	fb_blank = *(int *)evdata->data;

	dev_info(mdnie->dev, "%s: %d\n", __func__, fb_blank);

	if (evdata->info->node != 0)
		return 0;

	if (fb_blank == FB_BLANK_UNBLANK) {
		mutex_lock(&mdnie->lock);
		mdnie->enable = 1;
		mutex_unlock(&mdnie->lock);

		mdnie_update(mdnie);
#if defined(CONFIG_PANEL_S6E3HA3_DYNAMIC) || defined(CONFIG_PANEL_S6E3HF3_DYNAMIC)
		mdnie->disable_trans_dimming = 0;
#endif
	} else if (fb_blank == FB_BLANK_POWERDOWN) {
		mutex_lock(&mdnie->lock);
		mdnie->enable = 0;
#if defined(CONFIG_PANEL_S6E3HA3_DYNAMIC) || defined(CONFIG_PANEL_S6E3HF3_DYNAMIC)
		mdnie->disable_trans_dimming = 1;
#endif
		mutex_unlock(&mdnie->lock);
	}

	return 0;
}

static int mdnie_register_fb(struct mdnie_info *mdnie)
{
	memset(&mdnie->fb_notif, 0, sizeof(mdnie->fb_notif));
	mdnie->fb_notif.notifier_call = fb_notifier_callback;
	return fb_register_client(&mdnie->fb_notif);
}

static struct mdnie_info *g_mdnie = NULL;
void update_mdnie_coordinate( u16 coordinate0, u16 coordinate1 )
{
	struct mdnie_info *mdnie = g_mdnie;
	int ret;
	int result[5] = {0,};
	int pi = get_panel_index_init();

	if( mdnie == NULL ) {
		pr_err( "%s : mdnie has not initialized\n", __func__ );
		return;
	}

	pr_info( "%s : reload MDNIE-MTP\n", __func__ );
	// record the second panel
	mdnie->coordinate[pi][0] = coordinate0;
	mdnie->coordinate[pi][1] = coordinate1;

	ret = get_panel_coordinate(mdnie, result);
	if (ret > 0)
		update_color_position(mdnie, ret);

	return;
}

int mdnie_register(struct device *p, void *data, mdnie_w w, mdnie_r r, u16 *coordinate)
{
	int ret = 0;
	struct mdnie_info *mdnie;
	int pi = get_panel_index_init();

	mdnie_class = class_create(THIS_MODULE, "mdnie");
	if (IS_ERR_OR_NULL(mdnie_class)) {
		pr_err("failed to create mdnie class\n");
		ret = -EINVAL;
		goto error0;
	}

	mdnie_class->dev_attrs = mdnie_attributes;

	mdnie = kzalloc(sizeof(struct mdnie_info), GFP_KERNEL);
	if (!mdnie) {
		pr_err("failed to allocate mdnie\n");
		ret = -ENOMEM;
		goto error1;
	}

	g_mdnie = mdnie;

	mdnie->dev = device_create(mdnie_class, p, 0, &mdnie, "mdnie");
	if (IS_ERR_OR_NULL(mdnie->dev)) {
		pr_err("failed to create mdnie device\n");
		ret = -EINVAL;
		goto error2;
	}

	mdnie->scenario = UI_MODE;
	mdnie->mode = STANDARD;
	mdnie->enable = 0;
	mdnie->tuning = 0;
	mdnie->accessibility = ACCESSIBILITY_OFF;
	mdnie->bypass = BYPASS_OFF;

#if defined(CONFIG_PANEL_S6E3HA3_DYNAMIC) || defined(CONFIG_PANEL_S6E3HF3_DYNAMIC)
	mdnie->disable_trans_dimming = 0;
#endif

	mdnie->data = data;
	mdnie->ops.write = w;
	mdnie->ops.read = r;
	// record the first panel
	mdnie->coordinate[pi][0] = coordinate[0];
	mdnie->coordinate[pi][1] = coordinate[1];

	mutex_init(&mdnie->lock);
	mutex_init(&mdnie->dev_lock);

	dev_set_drvdata(mdnie->dev, mdnie);

	mdnie_register_fb(mdnie);

	mdnie->enable = 1;
	mdnie_update(mdnie);

#ifdef CONFIG_FOLDER_DUAL_PANEL
	mdnie_update_directly(mdnie);
#endif
	dev_info(mdnie->dev, "registered successfully\n");

	return 0;

error2:
	kfree(mdnie);
error1:
	class_destroy(mdnie_class);
error0:
	return ret;
}

