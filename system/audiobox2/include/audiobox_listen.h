#ifndef __AUDIOBOX_LISTEN_H__
#define __AUDIOBOX_LISTEN_H__
#include <base.h>

#define AUDIOBOX_RPC_BASE		"audiobox-listen"
#define AUDIOBOX_RPC_DEFAULT	"audiobox-listen-default"
#define AUDIOBOX_RPC_DEFAULTMIC	"audiobox-listen-default_mic"
#define AUDIOBOX_RPC_BTCODEC	"audiobox-listen-btcodec"
#define AUDIOBOX_RPC_BTCODECMIC	"audiobox-listen-btcodecmic"
#define AUDIOBOX_RPC_END		"audiobox-listen-end"

#define AUDIO_RPC_MAX	12

#define AB_EVENT(sc, cmd, dev, priv, result) struct event_scatter (sc)[] = {	\
	    {  .buf = (char *)cmd, .len = 4 },										\
	    {  .buf = (char *)dev, .len = AB_DEV_LEN, },							\
		{  .buf = (char *)priv, .len = sizeof(*priv), },						\
		{  .buf = (char *)result, .len = sizeof(*result), },					\
		{  .buf = NULL, .len = 4,},											\
		{  .buf = NULL, .len = 4,},}


#define AB_EVENT_RESV(sc, cmd, dev, priv, result, resv) struct event_scatter (sc)[] = {	\
	    {  .buf = (char *)cmd, .len = 4 },										\
	    {  .buf = (char *)dev, .len = AB_DEV_LEN, },							\
		{  .buf = (char *)priv, .len = sizeof(*priv), },						\
		{  .buf = (char *)result, .len = sizeof(*result), },					\
		{  .buf = (char *)resv, .len = sizeof(*resv),},						\
		{  .buf = NULL, .len = 4,},}

#define AB_EVENT_RESV_PID(sc, cmd, dev, priv, result, resv, pid) struct event_scatter (sc)[] = {	\
	    {  .buf = (char *)cmd, .len = 4 },										\
	    {  .buf = (char *)dev, .len = AB_DEV_LEN, },							\
		{  .buf = (char *)priv, .len = sizeof(*priv), },						\
		{  .buf = (char *)result, .len = sizeof(*result), },					\
		{  .buf = (char *)resv, .len = sizeof(*resv), },						\
		{  .buf = (char *)pid, .len = sizeof(*pid),},}

#define AB_EVENT_SIZE(s)	(sizeof(s) / sizeof((s)[0]))
#define AB_GET_CMD(s)		(s[0].buf)
#define AB_GET_DEV(s)		(s[1].buf)
#define AB_GET_CHN(s)		(s[1].buf)
#define AB_GET_PRIV(s)		(s[2].buf)
#define AB_GET_RESULT(s)    (s[3].buf)
#define AB_GET_RESV(s)    	(s[4].buf)
#define AB_GET_PID(s)    	(s[5].buf)

#define AB_GET_RESVLEN(s)	(s[4].len)


#define AB_COUNT			6
#define AB_DEV_LEN			16		

#define AB_ID	4
#define AUDIO_RPC_MAX_BUF	128
#define AUDIO_EVENT_SCATTER	4

enum AB_CMD_TYPE {
	AB_GET_DEV_ATTR,
	AB_SET_DEV_ATTR,
	AB_GET_DEV_MODULE,
	AB_SET_DEV_MODULE,
	AB_GET_DEV_VOLUME,
	AB_SET_DEV_VOLUME,
	AB_ENABLE_DEV,
	AB_DISABLE_DEV,
	AB_GET_CHANNEL,
	AB_PUT_CHANNEL,
	AB_GET_CHN_FMT,
	AB_SET_CHN_FMT,
	AB_GET_CHN_MUTE,
	AB_SET_CHN_MUTE,
	AB_GET_CHN_VOLUME,
	AB_SET_CHN_VOLUME,
	AB_READ_CHN_FRAME,
	AB_WRITE_CHN_FRAME,
    AB_GET_CHN_INFO,
	AB_START_SERVICE,
	AB_STOP_SERVICE,
};

extern int audiobox_listener_init(void);
extern int audiobox_listener_deinit(void);
extern void audiobox_signal_handler(int sig);
extern int audiobox_inner_release_channel(audio_chn_t chn);
#endif