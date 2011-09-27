/*
** Copyright 2010, Adam Shanks (@ChainsDD)
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

#include <stdlib.h>
#include <sys/stat.h>
#include <limits.h>
#include <cutils/log.h>

#include <sqlite3.h>

#include "su.h"

// { int* pint; pint=(int*)data; ++(*pint); }

sqlite3 *database_init()
{
    sqlite3 *db;
    int version, rc, databaseStatus = 0;
    char *zErrMsg = 0;

    rc = sqlite3_open_v2(REQUESTOR_DATABASE_PATH, &db, SQLITE_OPEN_READONLY, NULL);
    if ( rc ) {
        LOGE("Couldn't open database: %s", sqlite3_errmsg(db));
        return NULL;
    }

    // Create an automatic busy handler in case the db is locked
    sqlite3_busy_timeout(db, 1000);
    return db;
}

int database_check(sqlite3 *db, struct su_initiator *from, struct su_request *to)
{
    char sql[4096];
    char *zErrmsg;
    char **result;
    int nrow,ncol;
    int allow;
    struct timeval tv;

    sqlite3_snprintf(
        sizeof(sql), sql,
        "SELECT _id,name,allow FROM apps WHERE uid=%u AND exec_uid=%u AND exec_cmd='%q';",
        (unsigned)from->uid, to->uid, to->command
    );

    if (strlen(sql) >= sizeof(sql)-1)
        return DB_DENY;
        
    int error = sqlite3_get_table(db, sql, &result, &nrow, &ncol, &zErrmsg);
    if (error != SQLITE_OK) {
        LOGE("Database check failed with error message %s", zErrmsg);
        if (error == SQLITE_BUSY) {
            LOGE("Specifically, the database is busy");
        }
        return DB_DENY;
    }
    
    if (nrow == 0 || ncol != 3)
        return DB_INTERACTIVE;
        
    if (strcmp(result[0], "_id") == 0 && strcmp(result[2], "allow") == 0) {
        if (strcmp(result[5], "1") == 0) {
            allow = DB_ALLOW;
        } else if (strcmp(result[5], "-1") == 0){
            allow = DB_INTERACTIVE;
        } else {
            allow = DB_DENY;
        }
        return allow;
    }

    sqlite3_free_table(result);
    
    return DB_INTERACTIVE;
}
