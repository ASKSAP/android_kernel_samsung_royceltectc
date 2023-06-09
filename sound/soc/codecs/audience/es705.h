/*
 * es705.h  --  ES705 Soc Audio driver
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef _ES705_H
#define _ES705_H

#include <linux/cdev.h>
#include <linux/mutex.h>
#include <linux/time.h>

#include <sound/soc.h>
#include <sound/pcm.h>

#include "es705-uart.h"

#define SAMSUNG_ES705_FEATURE

#define PARSE_BUFFER_SIZE PAGE_SIZE
/* TODO: condition of kernel version or commit code to specific kernels */
#define ES705_DAI_ID_BASE	0
#define DAI_INDEX(xid)		(xid)

#define ES705_READ_VE_OFFSET		0x0804
#define ES705_READ_VE_WIDTH		4
#define ES705_WRITE_VE_OFFSET		0x0800
#define ES705_WRITE_VE_WIDTH		4

#define ES705_MCLK_DIV			0x0000
#define ES705_CLASSD_CLK_DIV		0x0001
#define ES705_CP_CLK_DIV		0x0002

#define ES705_BOOT_CMD			0x0001
#define ES705_BOOT_ACK			0x01010101

#define ES705_WDB_CMD			0x802f
#define ES705_RDB_CMD			0x802e
#define ES705_WDB_MAX_SIZE		512

#define ES705_SYNC_CMD			0x8000
#define ES705_SYNC_POLLING		0x0000
#define ES705_SYNC_INTR_ACITVE_LOW	0x0001
#define ES705_SYNC_INTR_ACITVE_HIGH	0x0002
#define ES705_SYNC_INTR_FALLING_EDGE	0x0003
#define ES705_SYNC_INTR_RISING_EDGE	0x0004
#define ES705_SYNC_ACK			0x80000000
#define ES705_SBL_ACK			0x8000FFFF

#define ES705_RESET_CMD			0x8002
#define ES705_RESET_IMMED		0x0000
#define ES705_RESET_DELAYED		0x0001

#define ES705_SET_POWER_STATE		0x9010
#define ES705_SET_POWER_STATE_SLEEP	0x0001
#define ES705_SET_POWER_STATE_MP_SLEEP	0x0002
#define ES705_SET_POWER_STATE_MP_CMD	0x0003
#define ES705_SET_POWER_STATE_NORMAL	0x0004
#define ES705_SET_POWER_STATE_VS_OVERLAY	0x0005
#define ES705_SET_POWER_STATE_VS_LOWPWR	0x0006
#define ES705_SET_POWER_STATE_VS_STREAMING	0x0007

#define ES705_STREAM_UART_ON		0x90250101
#define ES705_STREAM_UART_OFF		0x90250100

#define ES705_SET_SMOOTH			0x904E
#define ES705_SET_SMOOTH_RATE		0x0000

#define ES705_SET_SMOOTH_MUTE_ZERO	0x804E0000
#define ES705_SET_PRESET		0x8031
#define ES705_SET_POWER_LEVEL		0x8011
#define ES705_POWER_LEVEL_6			0x0006
#define ES705_GET_POWER_LEVEL		0x8012
#define ES705_DMIC_PRESET		0x0F08
/*
 * bit15 - reserved
 * bit[14:12] - access type
 * bit11 - commit = 0, staged = 1
 * bit[10:0] - psuedo address
 */
#define ES705_ACCESS_MASK	(7 << 12)
#define ES705_ALGO_ACCESS	(0 << 12)
#define ES705_DEV_ACCESS	(1 << 12)
#define ES705_CMD_ACCESS	(2 << 12)
#define ES705_OTHER_ACCESS	(3 << 12)

#define ES705_CMD_MASK		(1 << 11)
#define ES705_STAGED_CMD	(1 << 11)
#define ES705_COMMIT_CMD	(0 << 11)

#define ES705_ADDR_MASK		0x7ff

#define ES705_STAGED_MSG_BIT	(1 << 13)
/*
 * Device parameter command codes
 */
#define ES705_DEV_PARAM_OFFSET		0x2000
#define ES705_GET_DEV_PARAM		0x800b
#define ES705_SET_DEV_PARAM_ID		0x900c
#define ES705_SET_DEV_PARAM		0x900d

/*
 * Algoithm parameter command codes
 */
#define ES705_ALGO_PARAM_OFFSET		0x0000
#define ES705_GET_ALGO_PARAM		0x8016
#define ES705_SET_ALGO_PARAM_ID		0x9017
#define ES705_SET_ALGO_PARAM		0x9018

/*
 * addresses
 */
enum {
	ES705_MIC_CONFIG,
	ES705_AEC_MODE,
	ES705_TX_AGC,
	ES705_TX_AGC_TARGET_LEVEL,
	ES705_TX_AGC_NOISE_FLOOR,
	ES705_TX_AGC_SNR_IMPROVEMENT,
	ES705_VEQ_ENABLE,
	ES705_RX_OUT_LIMITER_MAX_LEVEL,
	ES705_RX_NOISE_SUPPRESS,
	ES705_RX_STS,
	ES705_RX_STS_RATE,
	ES705_AEC_SPEAKER_VOLUME,
	ES705_SIDETONE,
	ES705_SIDETONE_GAIN,
	ES705_TX_COMFORT_NOISE,
	ES705_TX_COMFORT_NOISE_LEVEL,
	ES705_ALGORITHM_RESET,
	ES705_RX_POST_EQ,
	ES705_TX_POST_EQ,
	ES705_AEC_CNG,
	ES705_VEQ_NOISE_ESTIMATION_ADJUSTMENT,
	ES705_TX_AGC_SLEW_RATE_UP,
	ES705_TX_AGC_SLEW_RATE_DOWN,
	ES705_RX_AGC,
	ES705_RX_AGC_TARGET_LEVEL,
	ES705_RX_AGC_NOISE_FLOOR,
	ES705_RX_AGC_SNR_IMPROVEMENT,
	ES705_RX_AGC_SLEW_RATE_UP,
	ES705_RX_AGC_SLEW_RATE_DOWN,
	ES705_AEC_CNG_GAIN,
	ES705_TX_MBC,
	ES705_RX_MBC,
	ES705_AEC_ESE,
	ES705_TX_NS_ADAPTATION_SPEED,
	ES705_TX_SNR_ESTIMATE,
	ES705_VEQ_MAX_GAIN,
	ES705_TX_AGC_GUARDBAND,
	ES705_RX_AGC_GUARDBAND,
	ES705_TX_OUT_LIMITER_MAX_LEVEL,
	ES705_TX_IN_LIMITER_MAX_LEVEL,
	ES705_RX_NS_ADAPTATION_SPEED,
	ES705_AEC_VARIABLE_ECHO_REF_DELAY,
	ES705_TX_NOISE_SUPPRESS_LEVEL,
	ES705_RX_NOISE_SUPPRESS_LEVEL,
	ES705_RX_CNG,
	ES705_RX_CNG_GAIN,
	ES705_TX_AGC_MAX_GAIN,
	ES705_RX_AGC_MAX_GAIN,
	ES705_AVALON_API_VERSION_LO,
	ES705_AVALON_API_VERSION_HI,
	ES705_AVALON_AV_PROCESSOR,
	ES705_AVALON_AV_CONFIG,
	ES705_AVALON_EQ_PRESET,
	ES705_AVALON_STEREO_WIDTH,
	ES705_AVALON_AV_DIGITAL_OUT_GAIN,
	ES705_AVALON_TDMBC,
	ES705_AVALON_AV_OUT_LIMIT,
	ES705_AVALON_STEREO_WIDENING,
	ES705_AVALON_STAT_NS,
	ES705_AVALON_STAT_NS_SUPPRESS,
	ES705_AVALON_STAT_NS_ADAP_SPEED,
	ES705_AVALON_STAT_NS_MODE,
	ES705_AVLALON_STAT_NS_MAX_NOISE_ENERGY,
	ES705_AVALON_VBB,
	ES705_AVALON_VBB_STRENGTH,
	ES705_AVALON_EQ_MODE,
	ES705_AVALON_EQ_GRAPHIC_BAND1_GAIN,
	ES705_AVALON_EQ_GRAPHIC_BAND2_GAIN,
	ES705_AVALON_EQ_GRAPHIC_BAND3_GAIN,
	ES705_AVALON_EQ_GRAPHIC_BAND4_GAIN,
	ES705_AVALON_EQ_GRAPHIC_BAND5_GAIN,
	ES705_AVALON_EQ_GRAPHIC_BAND6_GAIN,
	ES705_AVALON_EQ_GRAPHIC_BAND7_GAIN,
	ES705_AVALON_EQ_GRAPHIC_BAND8_GAIN,
	ES705_AVALON_EQ_GRAPHIC_BAND9_GAIN,
	ES705_AVALON_EQ_GRAPHIC_BAND10_GAIN,
	ES705_AVALON_TDDRC,
	ES705_AVALON_TDDRC_STRENGTH,
	ES705_AVALON_LIMITER,
	ES705_AVALON_EQ,
	ES705_DIRAC,
	ES705_DIRAC_OUT_HEADROOM_LIMITER,
	ES705_DIRAC_MODE,
	ES705_DIRAC_IN_HEADROOM_LIMITER,
	ES705_DIRAC_COMFORT_NOISE,
	ES705_DIRAC_COMFORT_NOISE_LEVEL,
	ES705_DIRAC_NARRATOR_VQOS,
	ES705_DIRAC_NARRATOR_POSITION_SUPPRESS,
	ES705_DIRAC_NARRATOR_AGC_OUT,
	ES705_DIRAC_NARRATOR_AGC_SPEECH_TARGET,
	ES705_DIRAC_NARRATOR_AGC_SNR_IMPROVE,
	ES705_DIRAC_NARRATOR_AGC_NOISE_FLOOR,
	ES705_DIRAC_NARRATOR_AGC_MAX_GAIN,
	ES705_DIRAC_NARRATOR_AGC_UP_RATE,
	ES705_DIRAC_NARRATOR_AGC_DOWN_RATE,
	ES705_DIRAC_NARRATOR_AGC_GUARDBAND,
	ES705_DIRAC_NARRATOR_POST_EQ_MODE,
	ES705_DIRAC_NARRATOR_MBC_MODE,
	ES705_DIRAC_SCENE_BEAM_WIDTH,
	ES705_DIRAC_SCENE_AGC_OUT,
	ES705_DIRAC_SCENE_AGC_SPEECH_TARGET,
	ES705_DIRAC_SCENE_AGC_SNR_IMPROVE,
	ES705_DIRAC_SCENE_AGC_NOISE_FLOOR,
	ES705_DIRAC_SCENE_AGC_MAX_GAIN,
	ES705_DIRAC_SCENE_AGC_UP_RATE,
	ES705_DIRAC_SCENE_AGC_DOWN_RATE,
	ES705_DIRAC_SCENE_AGC_GUARDBAND,
	ES705_DIRAC_SCENE_VQOS,
	ES705_DIRAC_SCENE_POST_EQ_MODE,
	ES705_DIRAC_SCENE_MBC_MODE,
	ES705_TONE_PARAM_API_VERSION_LO,
	ES705_TONE_PARAM_API_VERSION_HI,
	ES705_TONE_PARAM_ENABLE_BEEP_SYS,
	ES705_TONE_PARAM_ENABLE_GEN_BEEP,
	ES705_TONE_PARAM_GEN_BEEP_ON,
	ES705_TONE_PARAM_GEN_BEEP_FREQ1,
	ES705_TONE_PARAM_GEN_BEEP_FREQ2,
	ES705_TONE_PARAM_GEN_BEEP_PAN_LR,
	ES705_TONE_PARAM_GEN_BEEP_GAIN,
	ES705_DEREVERB_ENABLE,
	ES705_DEREVERB_GAIN,
	ES705_BWE_ENABLE,
	ES705_BWE_HIGH_BAND_GAIN,
	ES705_BWE_MAX_SNR,
	ES705_BWE_POST_EQ_ENABLE,
	ES705_PORTA_WORD_LEN,
	ES705_PORTA_TDM_SLOTS_PER_FRAME,
	ES705_PORTA_TX_DELAY_FROM_FS,
	ES705_PORTA_RX_DELAY_FROM_FS,
	ES705_PORTA_LATCH_EDGE,
	ES705_PORTA_ENDIAN,
	ES705_PORTA_TRISTATE,
	ES705_PORTA_AUDIO_PORT_MODE,
	ES705_PORTA_TDM_ENABLED,
	ES705_PORTA_CLOCK_CONTROL,
	ES705_PORTA_DATA_JUSTIFICATION,
	ES705_PORTA_FS_DURATION,
	ES705_PORTB_WORD_LEN,
	ES705_PORTB_TDM_SLOTS_PER_FRAME,
	ES705_PORTB_TX_DELAY_FROM_FS,
	ES705_PORTB_RX_DELAY_FROM_FS,
	ES705_PORTB_LATCH_EDGE,
	ES705_PORTB_ENDIAN,
	ES705_PORTB_TRISTATE,
	ES705_PORTB_AUDIO_PORT_MODE,
	ES705_PORTB_TDM_ENABLED,
	ES705_PORTB_CLOCK_CONTROL,
	ES705_PORTB_DATA_JUSTIFICATION,
	ES705_PORTB_FS_DURATION,
	ES705_PORTC_WORD_LEN,
	ES705_PORTC_TDM_SLOTS_PER_FRAME,
	ES705_PORTC_TX_DELAY_FROM_FS,
	ES705_PORTC_RX_DELAY_FROM_FS,
	ES705_PORTC_LATCH_EDGE,
	ES705_PORTC_ENDIAN,
	ES705_PORTC_TRISTATE,
	ES705_PORTC_AUDIO_PORT_MODE,
	ES705_PORTC_TDM_ENABLED,
	ES705_PORTC_CLOCK_CONTROL,
	ES705_PORTC_DATA_JUSTIFICATION,
	ES705_PORTC_FS_DURATION,
	ES705_PORTD_WORD_LEN,
	ES705_PORTD_TDM_SLOTS_PER_FRAME,
	ES705_PORTD_TX_DELAY_FROM_FS,
	ES705_PORTD_RX_DELAY_FROM_FS,
	ES705_PORTD_LATCH_EDGE,
	ES705_PORTD_ENDIAN,
	ES705_PORTD_TRISTATE,
	ES705_PORTD_AUDIO_PORT_MODE,
	ES705_PORTD_TDM_ENABLED,
	ES705_PORTD_CLOCK_CONTROL,
	ES705_PORTD_DATA_JUSTIFICATION,
	ES705_PORTD_FS_DURATION,
	ES705_SLIMBUS_LINK_MULTI_CHANNEL,
	ES705_POWER_STATE,
	ES705_STREAMING,
	ES705_FE_STREAMING,
	ES705_PRESET,
	ES705_ALGO_STATS,
	ES705_ALGO_PROCESSING,
	ES705_ALGO_SAMPLE_RATE,
	ES705_SMOOTH_RATE,
	ES705_CHANGE_STATUS,
	ES705_DIGITAL_PASS_THROUGH,
	ES705_DATA_PATH,
	ES705_ALGORITHM,
	ES705_MIX_SAMPLE_RATE,
	ES705_SIGNAL_RMS_PORTA_DIN_LEFT,
	ES705_SIGNAL_RMS_PORTA_DIN_RIGHT,
	ES705_SIGNAL_RMS_PORTA_DOUT_LEFT,
	ES705_SIGNAL_RMS_PORTA_DOUT_RIGHT,
	ES705_SIGNAL_RMS_PORTB_DIN_LEFT,
	ES705_SIGNAL_RMS_PORTB_DIN_RIGHT,
	ES705_SIGNAL_RMS_PORTB_DOUT_LEFT,
	ES705_SIGNAL_RMS_PORTB_DOUT_RIGHT,
	ES705_SIGNAL_RMS_PORTC_DIN_LEFT,
	ES705_SIGNAL_RMS_PORTC_DIN_RIGHT,
	ES705_SIGNAL_RMS_PORTC_DOUT_LEFT,
	ES705_SIGNAL_RMS_PORTC_DOUT_RIGHT,
	ES705_SIGNAL_RMS_PORTD_DIN_LEFT,
	ES705_SIGNAL_RMS_PORTD_DIN_RIGHT,
	ES705_SIGNAL_RMS_PORTD_DOUT_LEFT,
	ES705_SIGNAL_RMS_PORTD_DOUT_RIGHT,
	ES705_SIGNAL_PEAK_PORTA_DIN_LEFT,
	ES705_SIGNAL_PEAK_PORTA_DIN_RIGHT,
	ES705_SIGNAL_PEAK_PORTA_DOUT_LEFT,
	ES705_SIGNAL_PEAK_PORTA_DOUT_RIGHT,
	ES705_SIGNAL_PEAK_PORTB_DIN_LEFT,
	ES705_SIGNAL_PEAK_PORTB_DIN_RIGHT,
	ES705_SIGNAL_PEAK_PORTB_DOUT_LEFT,
	ES705_SIGNAL_PEAK_PORTB_DOUT_RIGHT,
	ES705_SIGNAL_PEAK_PORTC_DIN_LEFT,
	ES705_SIGNAL_PEAK_PORTC_DIN_RIGHT,
	ES705_SIGNAL_PEAK_PORTC_DOUT_LEFT,
	ES705_SIGNAL_PEAK_PORTC_DOUT_RIGHT,
	ES705_SIGNAL_PEAK_PORTD_DIN_LEFT,
	ES705_SIGNAL_PEAK_PORTD_DIN_RIGHT,
	ES705_SIGNAL_PEAK_PORTD_DOUT_LEFT,
	ES705_SIGNAL_PEAK_PORTD_DOUT_RIGHT,
	ES705_DIGITAL_GAIN_PRIMARY,
	ES705_DIGITAL_GAIN_SECONDARY,
	ES705_DIGITAL_GAIN_TERTIARY,
	ES705_DIGITAL_GAIN_QUAD,
	ES705_DIGITAL_GAIN_FEIN,
	ES705_DIGITAL_GAIN_AUDIN1,
	ES705_DIGITAL_GAIN_AUDIN2,
	ES705_DIGITAL_GAIN_AUDIN3,
	ES705_DIGITAL_GAIN_AUDIN4,
	ES705_DIGITAL_GAIN_UITONE1,
	ES705_DIGITAL_GAIN_UITONE2,
	ES705_DIGITAL_GAIN_CSOUT,
	ES705_DIGITAL_GAIN_FEOUT1,
	ES705_DIGITAL_GAIN_FEOUT2,
	ES705_DIGITAL_GAIN_AUDOUT1,
	ES705_DIGITAL_GAIN_AUDOUT2,
	ES705_DIGITAL_GAIN_AUDOUT3,
	ES705_DIGITAL_GAIN_AUDOUT4,
	ES705_PORTA_TIMING,
	ES705_PORTB_TIMING,
	ES705_PORTC_TIMING,
	ES705_PORTD_TIMING,
	ES705_FLUSH,
	ES705_FW_FIRST_CHAR,
	ES705_FW_NEXT_CHAR,
	ES705_EVENT_RESPONSE,
	ES705_VOICE_SENSE_ENABLE,
	ES705_VOICE_SENSE_SET_KEYWORD,
	ES705_VOICE_SENSE_EVENT,
	ES705_VOICE_SENSE_TRAINING_MODE,
	ES705_VOICE_SENSE_DETECTION_SENSITIVITY,
	ES705_VOICE_ACTIVITY_DETECTION_SENSITIVITY,
	ES705_VOICE_SENSE_TRAINING_RECORD,
	ES705_VOICE_SENSE_TRAINING_STATUS,
	ES705_VOICE_SENSE_DEMO_ENABLE,
	ES705_VS_STORED_KEYWORD,
	ES705_VS_INT_OSC_MEASURE_START,
	ES705_VS_INT_OSC_MEASURE_STATUS,
	ES705_CVS_PRESET,
	ES705_RX_ENABLE,
	ES705_VS_STREAM_ENABLE,
	ES705_API_ADDR_MAX,
};

#define ES705_SLIM_CH_RX_OFFSET		152
#define ES705_SLIM_CH_TX_OFFSET		156

/* #define ES705_SLIM_RX_PORTS		10 */
#if defined(SAMSUNG_ES705_FEATURE)
/*
 * RX => 0,1:FE_IN1 2,3:FE_IN2 4,5,6,7:BE_IN
 * TX => 0,1:FE_OUT 3,4:BE_OUT
 */
#define ES705_SLIM_RX_PORTS		8
#else
#define ES705_SLIM_RX_PORTS		6
#endif
#define ES705_SLIM_TX_PORTS		6
#define NUM_CODEC_SLIM_DAIS	6

#define NUM_CODEC_I2S_DAIS	4

#define ES705_I2S_PORTA		7
#define ES705_I2S_PORTB		8
#define ES705_I2S_PORTC		9
#define ES705_I2S_PORTD		10

#define ES705_NS_ON_PRESET		969
#define ES705_NS_OFF_PRESET		624
#define ES705_SW_ON_PRESET		702
#define ES705_SW_OFF_PRESET		703
#define ES705_STS_ON_PRESET		984
#define ES705_STS_OFF_PRESET	985
#define ES705_RX_NS_ON_PRESET	996
#define ES705_RX_NS_OFF_PRESET	997
#define ES705_WNF_ON_PRESET		994
#define ES705_WNF_OFF_PRESET	995
#define ES705_BWE_ON_PRESET		622
#define ES705_BWE_OFF_PRESET	623
#define ES705_AVALON_WN_ON_PRESET	704
#define ES705_AVALON_WN_OFF_PRESET	705
#define ES705_VBB_ON_PRESET		706
#define ES705_VBB_OFF_PRESET	707

#define ES705_VEQ_ON_PRESET		105
#define ES705_VEQ_OFF_PRESET	106

#define ES705_VS_PRESET		1382

#define ES705_AUD_ZOOM_PRESET			1355
#define ES705_AUD_ZOOM_NARRATOR_PRESET	756
#define ES705_AUD_ZOOM_SCENE_PRESET		757
#define ES705_AUD_ZOOM_NARRATION_PRESET	758

#define ES705_NUM_CODEC_DAIS (NUM_CODEC_SLIM_DAIS + NUM_CODEC_I2S_DAIS)

enum {
	ES705_SLIM_1_PB = ES705_DAI_ID_BASE,
	ES705_SLIM_1_CAP,
	ES705_SLIM_2_PB,
	ES705_SLIM_2_CAP,
	ES705_SLIM_3_PB,
	ES705_SLIM_3_CAP,
};

enum {
	SBL,
	STANDARD,
	VOICESENSE,
};

enum {
	NO_EVENT,
	CODEC_EVENT,
	KW_DETECTED,
};

#define ES705_SLIM_INTF		0
#define ES705_I2C_INTF		1
#define ES705_SPI_INTF         2
#define ES705_UART_INTF		3

struct es705_slim_dai_data {
	unsigned int rate;
	unsigned int *ch_num;
	unsigned int ch_act;
	unsigned int ch_tot;
};

struct es705_slim_ch {
	u32 sph;
	u32 ch_num;
	u16 ch_h;
	u16 grph;
};

enum {
	ES705_PM_ACTIVE,
	ES705_PM_SUSPENDING,
	ES705_PM_SUSPENDED,
	ES705_PM_SUSPENDING_TO_VS,
	ES705_PM_SUSPENDED_VS
};

enum {
	ES705_AUD_ZOOM_DISABLED,
	ES705_AUD_ZOOM_NARRATOR,
	ES705_AUD_ZOOM_SCENE,
	ES705_AUD_ZOOM_NARRATION
};

/*
 * Default delay before switch to sleep - 2sec
 * To change delay value use SYSFS sleep delay entry
 */
#define ES705_SLEEP_DEFAULT_DELAY 2000

/* Maximum size of keyword parameter block in bytes. */
#define ES705_VS_KEYWORD_PARAM_MAX 512

/* Base name used by character devices. */
#define ES705_CDEV_NAME "adnc"

/* device ops table for streaming operations */
struct es_stream_device {
	int (*open) (struct es705_priv *es705);
	int (*read) (struct es705_priv *es705, void *buf, int len);
	int (*close) (struct es705_priv *es705);
	int (*wait) (struct es705_priv *es705);
	int (*config)(struct es705_priv *es705);
	int intf;
#if defined(CONFIG_SND_SOC_ES_STREAM_FS_STORER)
	int (*fs_open)(struct es705_priv *es705);
	int (*fs_write)(struct es705_priv *es705, const void *buf, int len);
	void (*fs_close)(struct es705_priv *es705);
	struct file *fp;
	struct file *dev_fp;
	int cnt;
	int always_on;
	int route_status;
	int streaming;
#endif
};

struct es_datablock_device {
	int (*open) (struct es705_priv *es705);
	int (*read) (struct es705_priv *es705, void *buf, int len);
	int (*close) (struct es705_priv *es705);
	int (*wait) (struct es705_priv *es705);
	int (*config)(struct es705_priv *es705);
	int (*rdb) (struct es705_priv *es705, void *buf, int id);
	int (*wdb) (struct es705_priv *es705, const void *buf, int len);
	int intf;
};

struct es705_priv {
	struct device *dev;
	struct snd_soc_codec *codec;
	const struct firmware *standard;
	const struct firmware *vs;
#if defined(SAMSUNG_ES705_FEATURE)
	struct firmware *vs_grammar;
	struct firmware *vs_net;
	char grammar_path[100];
	char net_path[100];
#endif
	unsigned int intf;

	struct esxxx_platform_data *pdata;
	struct es_stream_device streamdev;
	struct es_datablock_device datablockdev;

	int (*dev_read) (struct es705_priv *es705, void *buf, int len);
	int (*dev_write) (struct es705_priv *es705, const void *buf, int len);
	int (*dev_write_then_read) (struct es705_priv *es705, const void *buf,
				    int len, u32 *rspn, int match);
	int (*vs_streaming) (struct es705_priv *es705);
	int (*boot_setup) (struct es705_priv *es705);
	int (*boot_finish) (struct es705_priv *es705);
	int (*uart_fw_download) (struct es705_priv *es705, int fw_type);
	int (*uart_es705_wakeup) (struct es705_priv *es705);
#if defined(CONFIG_SLIMBUS_MSM_NGD)
	int (*wakeup_bus) (struct es705_priv *es705);
#endif
	int (*rdb_wdb_open) (struct es705_priv *es705);
	int (*rdb_wdb_close) (struct es705_priv *es705);
	int (*rdb_read) (struct es705_priv *es705, void *buf, int len);
	int (*wdb_write) (struct es705_priv *es705, const void *buf, int len);

	struct timespec last_resp_time;
	u32 last_response;
	int (*cmd) (struct es705_priv *es705, u32 cmd, int sr, u32 *resp);

	struct i2c_client *i2c_client;
	struct slim_device *intf_client;
	struct slim_device *gen0_client;
	struct spi_device *spi_client;
	struct es705_uart_device uart_dev;

	struct mutex api_mutex;
	struct mutex cvq_mutex;
	struct mutex datablock_read_mutex;
	struct mutex streaming_mutex;
#if defined(CONFIG_SND_SOC_ES_STREAM_FS_STORER)
	struct mutex stream_fs_mutex;
	struct mutex stream_work_mutex;
	struct delayed_work stream_fs_start;
	struct work_struct stream_fs_stop;
#endif

	struct delayed_work sleep_work;
	struct es705_slim_dai_data dai[NUM_CODEC_SLIM_DAIS];
	struct es705_slim_ch slim_rx[ES705_SLIM_RX_PORTS];
	struct es705_slim_ch slim_tx[ES705_SLIM_TX_PORTS];

	struct mutex pm_mutex;
	int pm_state;
	int ns;		/*Noise suppression flag, used for read status */
	int zoom;	/*Audio Zoom status */
	int mode;
	int fw_requested;
	int vs_get_event;
	int vs_enable;
	int vs_wakeup_keyword;
	bool no_more_bit;
	int vs_stream_enable;
	int uart_fw_download_rate;
	int uart_state;
	int sleep_delay;
	u16 vs_keyword_param_size;
	u8 vs_keyword_param[ES705_VS_KEYWORD_PARAM_MAX];
	char *rdb_read_buffer;
	int rdb_read_count;
	u16 cvs_preset;
	u16 detected_vs_keyword;
	u16 vs_keyword_length;

	u16 preset;

	long internal_route_num;
	ssize_t internal_rate;
	size_t rx1_route_enable;
	size_t tx1_route_enable;
	size_t rx2_route_enable;

	unsigned int ap_tx1_ch_cnt;

	unsigned int es705_power_state;

	struct cdev cdev_command;
	struct cdev cdev_streaming;
	struct cdev cdev_firmware;
	struct cdev cdev_datablock;
	struct cdev cdev_datalogging;
	unsigned int datablock_intf;

	struct task_struct *stream_thread;
	wait_queue_head_t stream_in_q;

	/*es705 wakeup method : 1 - UART, 0 - wakeup gpio (default) */
	unsigned int wakeup_method;

#if defined(SAMSUNG_ES705_FEATURE)
	struct input_dev *input;
	int (*power_control) (unsigned int value, unsigned int reg);
	unsigned int voice_wakeup_enable;
	unsigned int voice_lpm_enable;
	unsigned int change_uart_config;
	unsigned int vs_grammar_set_flag;
	unsigned int vs_net_set_flag;
	int current_bwe;
	int current_veq;
	int current_veq_preset;
#endif
#if defined(CONFIG_SND_SOC_ESXXX_SEAMLESS_VOICE_WAKEUP)
	struct class *svoice;
	struct device *keyword;
	unsigned int keyword_type;
#endif

};

extern struct es705_priv es705_priv;
extern struct snd_soc_codec_driver soc_codec_dev_es705;
#if defined(CONFIG_SND_SOC_ES_SLIM)
extern struct snd_soc_dai_driver es705_dai[];
#endif
extern int es705_core_init(struct device *dev);
extern int es705_bootup(struct es705_priv *es705);

#if defined(SAMSUNG_ES705_FEATURE)
extern void msm_slim_es705_func(struct slim_device *gen0_client);
#endif

#define es705_resp(obj) ((obj)->last_response)
int es705_cmd(struct es705_priv *es705, u32 cmd);
int es705_slimbus_init(struct es705_priv *es705);
irqreturn_t es705_irq_event(int irq, void *irq_data);
int es705_put_veq_block(int volume);

extern int fw_download(void *arg);

extern u32 es705_streaming_cmds[];

#if defined(CONFIG_SND_SOC_ES_STREAM_FS_STORER)
extern void es705_stream_fs_storer_run(struct es705_priv *es705);
extern void es705_stream_fs_storer_stop(struct es705_priv *es705);
extern int stream_fs_open(struct es705_priv *es705);
extern void stream_fs_close(struct es705_priv *es705);
extern int stream_fs_write(struct es705_priv *es705, const void *buf, int len);
#endif
#define ES705_STREAM_DISABLE	0
#define ES705_STREAM_ENABLE	1
#define ES705_DATALOGGING_CMD_ENABLE	0x803f0001
#define ES705_DATALOGGING_CMD_DISABLE	0x803f0000

#ifdef SAMSUNG_ES705_FEATURE
#define ES_SYNC_MAX_RETRY 5
#define ES_SYNC_CMD			0x8000
#define ES_SYNC_POLLING			0x0000
#define ES_SYNC_ACK			0x80000000
#define ES_NO_EVENT			0x0000
#define ES_SET_POWER_LEVEL		0x8011
#define ES_POWER_LEVEL_6		0x0006
#endif

#define ES705_RATES (SNDRV_PCM_RATE_8000 | SNDRV_PCM_RATE_11025 |\
			SNDRV_PCM_RATE_16000 | SNDRV_PCM_RATE_22050 |\
			SNDRV_PCM_RATE_32000 | SNDRV_PCM_RATE_48000 |\
			SNDRV_PCM_RATE_96000 | SNDRV_PCM_RATE_192000)
#define ES705_SLIMBUS_RATES (SNDRV_PCM_RATE_48000)

#define ES705_FORMATS (SNDRV_PCM_FMTBIT_S16_LE | SNDRV_PCM_FMTBIT_S16_BE |\
			SNDRV_PCM_FMTBIT_S20_3LE | SNDRV_PCM_FMTBIT_S20_3BE |\
			SNDRV_PCM_FMTBIT_S24_LE | SNDRV_PCM_FMTBIT_S24_BE |\
			SNDRV_PCM_FMTBIT_S32_LE | SNDRV_PCM_FMTBIT_S32_BE)
#define ES705_SLIMBUS_FORMATS (SNDRV_PCM_FMTBIT_S16_LE |\
			SNDRV_PCM_FMTBIT_S16_BE)
#endif /* _ES705_H */
