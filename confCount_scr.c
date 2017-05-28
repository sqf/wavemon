/*
 * wavemon - a wireless network monitoring aplication
 *
 * Copyright (c) 2001-2002 Jan Morgenstern <jan@jm-music.de>
 *
 * wavemon is free software; you can redistribute it and/or modify it under
 * the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 2, or (at your option) any later
 * version.
 *
 * wavemon is distributed in the hope that it will be useful, but WITHOUT ANY
 * WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 * FOR A PARTICULAR PURPOSE.  See the GNU General Public License for more
 * details.
 *
 * You should have received a copy of the GNU General Public License along
 * with wavemon; see the file COPYING.  If not, write to the Free Software
 * Foundation, 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 */
#include "iw_if.h"
#include "iw_nl80211.h"
#include <time.h>
#include <sys/time.h>
#include <string.h>

////#include "info_scr.c"
//
WINDOW *w_levels, *w_stats, *w_if, *w_info, *w_net;
//
// Global linkstat data, populated by sampling thread.
struct {
    bool              run;	 // enable/disable sampling
    pthread_mutex_t   mutex; // producer/consumer lock for @data
    struct iw_nl80211_linkstat data;
} linkstat;

static time_t last_update;
static pthread_t sampling_thread;

/** Sampling pthread shared by info and histogram screen. */
static void *sampling_loop(void *arg)
{
    sigset_t blockmask;

    /* See comment in scan_scr.c for rationale. */
    sigemptyset(&blockmask);
    sigaddset(&blockmask, SIGWINCH);
    pthread_sigmask(SIG_BLOCK, &blockmask, NULL);

    do {
        pthread_mutex_lock(&linkstat.mutex);
        iw_nl80211_get_linkstat(&linkstat.data);
        pthread_mutex_unlock(&linkstat.mutex);

        iw_cache_update(&linkstat.data);
    } while (linkstat.run && usleep(conf.stat_iv * 1000) == 0);
    return NULL;
}

//string getTime ()
//{
//	time_t rawtime;
//	struct tm * timeinfo;
//
//	time ( &rawtime );
//	timeinfo = localtime ( &rawtime );
//	printf ( "Current local time and date: %s", asctime (timeinfo) );
//
//	return asctime (timeinfo);
//}

void scr_confCount_init(void)
{
    int line = 0;

    w_if	 = newwin_title(line, WH_IFACE, "Interface", true);
    line += WH_IFACE;
    w_levels = newwin_title(line, 5, "Levels", true);
    line += 5;
    w_info	 = newwin_title(line, WH_INFO_MIN, "Info", true);

}


int scr_confCount_loop(WINDOW *w_menu)
{
    time_t now = time(NULL);

    return wgetch(w_menu);
}

void scr_confCount_fini(void)
{
    delwin(w_net);
    delwin(w_info);
    delwin(w_stats);
    delwin(w_levels);
    delwin(w_if);
}
