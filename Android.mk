LOCAL_PATH := $(call my-dir)
include $(CLEAR_VARS)

LOCAL_MODULE := su
LOCAL_SRC_FILES := su.c db.c activity.cpp


LOCAL_C_INCLUDES += external/sqlite/dist
LOCAL_SHARED_LIBRARIES := liblog libsqlite libandroid_runtime

LOCAL_MODULE_PATH := $(TARGET_OUT_OPTIONAL_EXECUTABLES)

include $(BUILD_EXECUTABLE)
