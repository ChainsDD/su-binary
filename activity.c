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

#include <unistd.h>
#include <stdlib.h>

#include "su.h"

int send_intent(const struct su_context *ctx,
                const char *socket_path, int allow, const char *action)
{
    char command[PATH_MAX];
    pid_t euid;
    gid_t egid;
    int rc;

	sprintf(command, "/system/bin/am broadcast -a '%s' --es socket '%s' --ei caller_uid '%d' --ei allow '%d' --ei version_code '%d' > /dev/null",
			action, socket_path, ctx->from.uid, allow, VERSION_CODE);	

    // before sending the intent, make sure the (uid and euid) and (gid and egid) match,
    // otherwise LD_LIBRARY_PATH is wiped in Android 4.0+.
    // Also, sanitize all secure environment variables (from linker_environ.c in linker).

    /* The same list than GLibc at this point */
    static const char* const unsec_vars[] = {
        "GCONV_PATH",
        "GETCONF_DIR",
        "HOSTALIASES",
        "LD_AUDIT",
        "LD_DEBUG",
        "LD_DEBUG_OUTPUT",
        "LD_DYNAMIC_WEAK",
        "LD_LIBRARY_PATH",
        "LD_ORIGIN_PATH",
        "LD_PRELOAD",
        "LD_PROFILE",
        "LD_SHOW_AUXV",
        "LD_USE_LOAD_BIAS",
        "LOCALDOMAIN",
        "LOCPATH",
        "MALLOC_TRACE",
        "MALLOC_CHECK_",
        "NIS_PATH",
        "NLSPATH",
        "RESOLV_HOST_CONF",
        "RES_OPTIONS",
        "TMPDIR",
        "TZDIR",
        "LD_AOUT_LIBRARY_PATH",
        "LD_AOUT_PRELOAD",
        // not listed in linker, used due to system() call
        "IFS",
    };
    const char* const* cp   = unsec_vars;
    const char* const* endp = cp + sizeof(unsec_vars)/sizeof(unsec_vars[0]);
    while (cp < endp) {
        unsetenv(*cp);
        cp++;
    }

    // sane value so "am" works
    setenv("LD_LIBRARY_PATH", "/vendor/lib:/system/lib", 1);
    euid = geteuid();
    egid = getegid();
    setegid(getgid());
    seteuid(getuid());
    rc = system(command);
    seteuid(0);
    setegid(egid);
    seteuid(euid);
	return rc;
}
