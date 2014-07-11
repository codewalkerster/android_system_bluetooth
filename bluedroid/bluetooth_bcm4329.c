/*
 * Copyright (C) 2008 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#define LOG_TAG "bluedroid"

#include <errno.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/stat.h>

#include <cutils/log.h>
#include <cutils/properties.h>

#include <bluetooth/bluetooth.h>
#include <bluetooth/hci.h>
#include <bluetooth/hci_lib.h>

#include <bluedroid/bluetooth.h>

//----------------------------------------------------------------------------------------------------------------
#ifndef HCI_DEV_ID
#define HCI_DEV_ID 0
#endif

#define HCID_STOP_DELAY_USEC 500000

#define MIN(x,y) (((x)<(y))?(x):(y))

//----------------------------------------------------------------------------------------------------------------
// ODROID
//----------------------------------------------------------------------------------------------------------------
#if defined(BCM4329_MODULE)
    #define	BT_RESET_CTL_FP 	"/sys/devices/platform/odroid-sysfs/bt_nrst"	// 1 -> reset on
#endif
    
#define	BT_ENABLE_CTL_FP  	"/sys/devices/platform/odroid-sysfs/bt_enable"	// 1 -> enable on
#define	BT_WAKE_CTL_FP		"/sys/devices/platform/odroid-sysfs/bt_wake"	// 1 -> wake on

#define LOGD    ALOGD
#define LOGI    ALOGI
#define LOGE    ALOGE
#define LOGW    ALOGW
#define LOGV    ALOGV

// return value : 1 -> success, -1 -> error
int bt_set_module_status	(char *ctl_fp, unsigned char status);

// return value : 1 -> on, 0 -> off, -1 -> error
int bt_get_module_status	(char *ctl_fp);

//----------------------------------------------------------------------------------------------------------------
static int check_bluetooth_power() {
	int		bt_reset, bt_enable, bt_wake;

#if defined(BCM4329_MODULE)
	if((bt_reset 	= bt_get_module_status(BT_RESET_CTL_FP)) 	< 0)	goto err_out;
#endif	    
	if((bt_enable 	= bt_get_module_status(BT_ENABLE_CTL_FP)) 	< 0)	goto err_out;
	if((bt_wake 	= bt_get_module_status(BT_WAKE_CTL_FP)) 	< 0)	goto err_out;

#if defined(BCM4329_MODULE)
	if(bt_reset && bt_enable && bt_wake)	return	1;
#else
	if(bt_enable && bt_wake)	return	1;
#endif	    
	else	{
		bt_set_module_status(BT_WAKE_CTL_FP		, 0);	bt_set_module_status(BT_ENABLE_CTL_FP	, 0);
#if defined(BCM4329_MODULE)
		bt_set_module_status(BT_RESET_CTL_FP	, 0);
#endif		
		return	0;
	}

err_out:
	if(bt_wake 		>= 0)	bt_set_module_status(BT_WAKE_CTL_FP		, 0);
	if(bt_enable	>= 0)	bt_set_module_status(BT_ENABLE_CTL_FP	, 0);
#if defined(BCM4329_MODULE)
	if(bt_reset 	>= 0)	bt_set_module_status(BT_RESET_CTL_FP	, 0);
#endif	    

	return	-1;

}

//----------------------------------------------------------------------------------------------------------------
static int set_bluetooth_power(int on) {
	int		bt_reset, bt_enable, bt_wake;

#if defined(BCM4329_MODULE)
	if((bt_reset 	= bt_set_module_status(BT_RESET_CTL_FP, on)) 	< 0)		goto err_out;
#endif	    
	if((bt_enable 	= bt_set_module_status(BT_ENABLE_CTL_FP, on)) 	< 0)		goto err_out;
	if((bt_wake 	= bt_set_module_status(BT_WAKE_CTL_FP, on)) 	< 0)		goto err_out;

#if defined(BCM4329_MODULE)
	if(bt_enable && bt_wake && bt_reset)		return	on;
#else	    
	if(bt_enable && bt_wake)		return	on;
#endif	    

err_out:
	if(bt_wake	)	bt_set_module_status(BT_WAKE_CTL_FP		, 0);
	if(bt_enable)	bt_set_module_status(BT_ENABLE_CTL_FP	, 0);
#if defined(BCM4329_MODULE)
	if(bt_reset	)	bt_set_module_status(BT_RESET_CTL_FP	, 0);
#endif	    
	
	return	-1;
}

//----------------------------------------------------------------------------------------------------------------
static inline int create_hci_sock() {
    int sk = socket(AF_BLUETOOTH, SOCK_RAW, BTPROTO_HCI);
    if (sk < 0) {
        LOGE("Failed to create bluetooth hci socket: %s (%d)",
             strerror(errno), errno);
    }
    return sk;
}

//----------------------------------------------------------------------------------------------------------------
int bt_enable() {
    LOGV(__FUNCTION__);

    int ret = -1;
    int hci_sock = -1;
    int attempt;

    if (set_bluetooth_power(1) < 0) goto out;

    LOGI("Starting hciattach daemon");
    if (property_set("ctl.start", "hciattach") < 0) {
        LOGE("Failed to start hciattach");
        set_bluetooth_power(0);
        goto out;
    }
    sleep(1);

    // Try for 20 seconds, this can only succeed once hciattach has sent the
    // firmware and then turned on hci device via HCIUARTSETPROTO ioctl
    for (attempt = 20; attempt > 0;  attempt--) {
        hci_sock = create_hci_sock();
        if (hci_sock < 0) goto out;

        ret = ioctl(hci_sock, HCIDEVUP, HCI_DEV_ID);

        LOGI("bt_enable: ret: %d, errno: %d", ret, errno);
        if (!ret) {
            break;
        } else if (errno == EALREADY) {
            LOGW("Bluetoothd already started, unexpectedly!");
            break;
        }

        close(hci_sock);
        sleep(1);  // 1 sec retry delay
    }
    if (attempt == 0) {
        LOGE("%s: Timeout waiting for HCI device to come up, error- %d, ",
            __FUNCTION__, ret);
        if (property_set("ctl.stop", "hciattach") < 0) {
            LOGE("Error stopping hciattach");
        }
        set_bluetooth_power(0);
        goto out;
    }

    LOGI("Starting bluetoothd deamon");
    if (property_set("ctl.start", "bluetoothd") < 0) {
        LOGE("Failed to start bluetoothd");
        set_bluetooth_power(0);
        goto out;
    }

    ret = 0;

out:
    if (hci_sock >= 0) close(hci_sock);
    return ret;
}

//----------------------------------------------------------------------------------------------------------------
int bt_disable() {
    LOGV(__FUNCTION__);

    int ret = -1;
    int hci_sock = -1;

    LOGI("Stopping bluetoothd deamon");
    if (property_set("ctl.stop", "bluetoothd") < 0) {
        LOGE("Error stopping bluetoothd");
        goto out;
    }
    usleep(HCID_STOP_DELAY_USEC);

    hci_sock = create_hci_sock();
    if (hci_sock < 0) goto out;
    ioctl(hci_sock, HCIDEVDOWN, HCI_DEV_ID);

    LOGI("Stopping hciattach deamon");
    if (property_set("ctl.stop", "hciattach") < 0) {
        LOGE("Error stopping hciattach");
        goto out;
    }

    if (set_bluetooth_power(0) < 0) {
        goto out;
    }
    ret = 0;

out:
    if (hci_sock >= 0) close(hci_sock);
    return ret;
}

//----------------------------------------------------------------------------------------------------------------
int bt_is_enabled() {
    LOGV(__FUNCTION__);

    int hci_sock = -1;
    int ret = -1;
    struct hci_dev_info dev_info;

    // Check power first
    ret = check_bluetooth_power();
    if (ret == -1 || ret == 0) goto out;

    ret = -1;

    // Power is on, now check if the HCI interface is up
    hci_sock = create_hci_sock();
    if (hci_sock < 0) goto out;

    dev_info.dev_id = HCI_DEV_ID;
    if (ioctl(hci_sock, HCIGETDEVINFO, (void *)&dev_info) < 0) {
        ret = 0;
        goto out;
    }

    if (dev_info.flags & (1 << (HCI_UP & 31))) {
        ret = 1;
    } else {
        ret = 0;
    }

out:
    if (hci_sock >= 0) close(hci_sock);
    return ret;
}

//----------------------------------------------------------------------------------------------------------------
int ba2str(const bdaddr_t *ba, char *str) {
    return sprintf(str, "%2.2X:%2.2X:%2.2X:%2.2X:%2.2X:%2.2X",
                ba->b[5], ba->b[4], ba->b[3], ba->b[2], ba->b[1], ba->b[0]);
}

//----------------------------------------------------------------------------------------------------------------
int str2ba(const char *str, bdaddr_t *ba) {
    int i;
    for (i = 5; i >= 0; i--) {
        ba->b[i] = (uint8_t) strtoul(str, &str, 16);
        str++;
    }
    return 0;
}

//----------------------------------------------------------------------------------------------------------------
// ODROID
//----------------------------------------------------------------------------------------------------------------
int bt_set_module_status(char *ctl_fp, unsigned char status)
{
	int 	fd, ret, nwr;
	char	buf[10];

    usleep(HCID_STOP_DELAY_USEC);

	if((fd = open(ctl_fp, O_RDWR)) < 0)	{
        LOGE("%s(%s) : Cannot access \"%s\"", __FILE__, __FUNCTION__, ctl_fp);
        return 	-1;	// fd open fail
	}
	
	memset((void *)buf, 0x00, sizeof(buf));

	if(status)	nwr = sprintf(buf, "%d\n", 1);
	else		nwr = sprintf(buf, "%d\n", 0);

	ret = write(fd, buf, nwr);

	close(fd);
	
	if(ret == nwr)	{
        LOGI("%s : write success (on = %d)", ctl_fp, status);
        return	1;
	}
	else	{
        LOGE("%s : write fail (on = %d)",  ctl_fp, status);
        return	-1;
	}
}

//----------------------------------------------------------------------------------------------------------------
int	bt_get_module_status(char *ctl_fp)
{
	int 	fd, ret, nrd;
	char	buf[10];
	
    usleep(HCID_STOP_DELAY_USEC);

	if((fd = open(ctl_fp, O_RDONLY)) < 0)	{
        LOGE("%s(%s) : Cannot access \"%s\"", __FILE__, __FUNCTION__, ctl_fp);
		return	-1;	// fd open fail
	}
	
	memset((void *)buf, 0x00, sizeof(buf));

	nrd = read(fd, buf, sizeof(buf));
	
	close(fd);

	// read ok
	if(nrd)	{
		if(!strncmp(buf, "1", 1))		{
		    LOGI("%s : status == 1", ctl_fp);
			return	1;	// wakeup
		}
		else	{
			LOGI("%s : status == 0", ctl_fp);
			return	0;	// suspend
		}
	}

	LOGI("%s(%s) : module status == unknown", __FILE__, __FUNCTION__);
	return	-1;
}

//----------------------------------------------------------------------------------------------------------------
