#
# libbluedroid
#

LOCAL_PATH:= $(call my-dir)

include $(CLEAR_VARS)

ifeq ($(BOARD_HAVE_BLUETOOTH_BCM),true)
LOCAL_SRC_FILES := \
	bluetooth_bcm4329.c
else
ifeq ($(BOARD_HAVE_BLUETOOTH_USB),true)
LOCAL_SRC_FILES := \
	bluetooth_usb.c
else
LOCAL_SRC_FILES := \
	bluetooth.c
endif
endif

LOCAL_C_INCLUDES := \
	$(LOCAL_PATH)/include \
	system/bluetooth/bluez-clean-headers

ifeq ($(BOARD_BLUETOOTH_BCM4329), true)
LOCAL_CFLAGS := -DBCM4329_MODULE
endif

LOCAL_SHARED_LIBRARIES := \
	libcutils

LOCAL_MODULE := libbluedroid

include $(BUILD_SHARED_LIBRARY)
