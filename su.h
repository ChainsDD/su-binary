/*
** Copyright 2010, Adam Shanks (@ChainsDD)
** Copyright 2008, Zinx Verituse (@zinxv)
**
** Licensed under the Apache License, Version 2.0 (the "License");
** you may not use this file except in compliance with the License.
** You may obtain a copy of the License at
**
**     http://www.apache.org/licenses/LICENSE-2.0
**
** Unless required by applicable law or agreed to in writing, software
** distributed under the License is distributed on an "AS IS" BASIS,
** WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
** See the License for the specific language governing permissions and
** limitations under the License.
*/

#ifndef SU_h 
#define SU_h 1

#define REQUESTOR_DATA_PATH "/data/data/com.noshufou.android.su"
#define REQUESTOR_CACHE_PATH REQUESTOR_DATA_PATH "/cache"

#define REQUESTOR_DATABASES_PATH REQUESTOR_DATA_PATH "/databases"
#define REQUESTOR_DATABASE_PATH REQUESTOR_DATABASES_PATH "/permissions.sqlite"

#define APPS_TABLE_DEFINITION "CREATE TABLE IF NOT EXISTS apps (_id INTEGER, uid INTEGER, package TEXT, name TEXT, exec_uid INTEGER, exec_cmd TEXT, allow INTEGER, PRIMARY KEY (_id), UNIQUE (uid,exec_uid,exec_cmd));"
#define LOGS_TABLE_DEFINITION "CREATE TABLE IF NOT EXISTS logs (_id INTEGER, uid INTEGER, name INTEGER, app_id INTEGER, date INTEGER, type INTEGER, PRIMARY KEY (_id));"
#define PREFS_TABLE_DEFINITION "CREATE TABLE IF NOT EXISTS prefs (_id INTEGER, key TEXT, value TEXT, PRIMARY KEY(_id));"

#define DEFAULT_COMMAND "/system/bin/sh"

#define SOCKET_PATH_TEMPLATE REQUESTOR_CACHE_PATH "/.socketXXXXXX"

#define VERSION "3.0.3l"
#define VERSION_CODE 14

#define DATABASE_VERSION 6

struct su_initiator {
    pid_t pid;
    unsigned uid;
    char bin[PATH_MAX];
    char args[4096];
};

struct su_request {
    unsigned uid;
    char *command;
};

enum {
    DB_INTERACTIVE,
    DB_DENY,
    DB_ALLOW
};

extern int send_intent(struct su_initiator *from, struct su_request *to, const char *socket_path, int allow, int type);

#if 0
#undef LOGE
#define LOGE(fmt,args...) fprintf(stderr, fmt , ## args )
#undef LOGD
#define LOGD(fmt,args...) fprintf(stderr, fmt , ## args )
#undef LOGW
#define LOGW(fmt,args...) fprintf(stderr, fmt , ## args )
#endif

#define PLOGE(fmt,args...) LOGE(fmt " failed with %d: %s" , ## args , errno, strerror(errno))
#define PLOGEV(fmt,err,args...) LOGE(fmt " failed with %d: %s" , ## args , err, strerror(err))

#endif
