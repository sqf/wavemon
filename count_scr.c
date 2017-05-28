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

void sampling_init2()
{
    linkstat.run = true;
    pthread_create(&sampling_thread, NULL, sampling_loop, NULL);
}

void sampling_stop2(void)
{
    linkstat.run = false;
    pthread_join(sampling_thread, NULL);
}


void scr_count_init(void)
{
    int line = 0;

    w_if	 = newwin_title(line, WH_IFACE, "Interface", true);
    line += WH_IFACE;
    w_levels = newwin_title(line, 5, "Levels", true);
    line += 5;
    w_info	 = newwin_title(line, WH_INFO_MIN, "Info", true);


    sampling_init2();
}

void display_levels(void)
{
    static float qual, signal, noise, ssnr;
    /*
     * FIXME: revise the scale implementation. It does not work
     *        satisfactorily, maybe it is better to have a simple
     *        solution using 3 levels of different colour.
     */
    int8_t nscale[2] = { conf.noise_min, conf.noise_max },
            lvlscale[2] = { -40, -20};
    char tmp[0x100];
    int line;
    bool noise_data_valid;
    int sig_qual = -1, sig_qual_max, sig_level;


    noise_data_valid = iw_nl80211_have_survey_data(&linkstat.data);
    //sig_level = linkstat.data.signal_avg ?: linkstat.data.signal;
    sig_level = linkstat.data.signal;

    /* See comments in iw_cache_update */
    if (sig_level == 0)
        sig_level = linkstat.data.bss_signal;

    for (line = 1; line <= WH_LEVEL; line++)
        mvwclrtoborder(w_levels, line, 1);

    if (linkstat.data.bss_signal_qual) {
        /* BSS_SIGNAL_UNSPEC is scaled 0..100 */
        sig_qual     = linkstat.data.bss_signal_qual;
        sig_qual_max = 100;
    } else if (sig_level) {
        if (sig_level < -110)
            sig_qual = 0;
        else if (sig_level > -40)
            sig_qual = 70;
        else
            sig_qual = sig_level + 110;
        sig_qual_max = 70;
    }

    if (sig_qual == -1 && !sig_level && !noise_data_valid) {
        wattron(w_levels, A_BOLD);
        waddstr_center(w_levels, (WH_LEVEL + 1)/2, "NO INTERFACE DATA");
        goto done_levels;
    }

    line = 1;

    if (!noise_data_valid)
        line++;

    if (sig_level != 0) {
        signal = ewma(signal, sig_level, conf.meter_decay / 100.0);

        mvwaddstr(w_levels, line++, 1, "signal level: ");
        sprintf(tmp, "%.0f dBm (%s)", signal, dbm2units(signal));
        waddstr_b(w_levels, tmp);

        waddbar(w_levels, line, signal, conf.sig_min, conf.sig_max,
                lvlscale, true);
        if (conf.lthreshold_action)
            waddthreshold(w_levels, line, signal, conf.lthreshold,
                          conf.sig_min, conf.sig_max, lvlscale, '>');
        if (conf.hthreshold_action)
            waddthreshold(w_levels, line, signal, conf.hthreshold,
                          conf.sig_min, conf.sig_max, lvlscale, '<');
    }

    line++;

    if (noise_data_valid) {
        noise = ewma(noise, linkstat.data.survey.noise, conf.meter_decay / 100.0);

        mvwaddstr(w_levels, line++, 1, "noise level:  ");
        sprintf(tmp, "%.0f dBm (%s)", noise, dbm2units(noise));
        waddstr_b(w_levels, tmp);

        waddbar(w_levels, line++, noise, conf.noise_min, conf.noise_max,
                nscale, false);
    }

    if (noise_data_valid && sig_level) {
        ssnr = ewma(ssnr, sig_level - linkstat.data.survey.noise,
                    conf.meter_decay / 100.0);

        mvwaddstr(w_levels, line++, 1, "SNR:           ");
        sprintf(tmp, "%.0f dB", ssnr);
        waddstr_b(w_levels, tmp);
    }

	FILE *fp;
    struct timeval tp;
    gettimeofday(&tp, 0);
    time_t curtime = tp.tv_sec;
    struct tm *t = localtime(&curtime);

    char* filePathWithName;
    filePathWithName = malloc(strlen(filePath)+strlen(fileName)); /* make space for the new string (should check the return value ...) */
    strcpy(filePathWithName, filePath); /* copy name into the new var */
    strcat(filePathWithName, fileName); /* add the extension */

	if ((fp=fopen(filePathWithName, "a"))==NULL) {
		printf ("Can't open file ntest.txt!\n");
		exit(1);
	}


	//printf("%d-%d-%d %02d:%02d:%02d:%06ld\n", t->tm_mday, t->tm_mon + 1, t->tm_year + 1900, t->tm_hour, t->tm_min, t->tm_sec, tp.tv_usec/1000);


	fprintf (fp, "%d-%d-%d %02d:%02d:%02d:%06ld", t->tm_year + 1900, t->tm_mon + 1, t->tm_mday, t->tm_hour, t->tm_min, t->tm_sec, tp.tv_usec/1);
	fprintf (fp, " %d", linkstat.data.signal);
	fprintf (fp, " %d", linkstat.data.signal_avg);
    fprintf (fp, " %d", linkstat.data.bss_signal);
    fprintf (fp, " %d\n", linkstat.data.bss_signal_qual);

	fclose (fp);

    done_levels:
    wrefresh(w_levels);
}

void display_stats(void)
{
    char tmp[0x100];

    /*
     * Interface RX stats
     */
    mvwaddstr(w_stats, 1, 1, "RX: ");

    if (linkstat.data.rx_packets) {
        sprintf(tmp, "%'u (%s)",  linkstat.data.rx_packets,
                byte_units(linkstat.data.rx_bytes));
        waddstr_b(w_stats, tmp);
    } else {
        waddstr(w_stats, "n/a");
    }

    if (iw_nl80211_have_survey_data(&linkstat.data)) {
        if (linkstat.data.rx_bitrate[0]) {
            waddstr(w_stats, ", rate: ");
            waddstr_b(w_stats, linkstat.data.rx_bitrate);
        }

        if (linkstat.data.expected_thru) {
            if (linkstat.data.expected_thru >= 1024)
                sprintf(tmp, " (expected: %.1f MB/s)",  linkstat.data.expected_thru/1024.0);
            else
                sprintf(tmp, " (expected: %u kB/s)",  linkstat.data.expected_thru);
            waddstr(w_stats, tmp);
        }
    }

    if (linkstat.data.rx_drop_misc) {
        waddstr(w_stats, ", drop: ");
        sprintf(tmp, "%'llu", (unsigned long long)linkstat.data.rx_drop_misc);
        waddstr_b(w_stats, tmp);
    }

    wclrtoborder(w_stats);

    /*
     * Interface TX stats
     */
    mvwaddstr(w_stats, 2, 1, "TX: ");

    if (linkstat.data.tx_packets) {
        sprintf(tmp, "%'u (%s)",  linkstat.data.tx_packets,
                byte_units(linkstat.data.tx_bytes));
        waddstr_b(w_stats, tmp);
    } else {
        waddstr(w_stats, "n/a");
    }

    if (iw_nl80211_have_survey_data(&linkstat.data) && linkstat.data.tx_bitrate[0]) {
        waddstr(w_stats, ", rate: ");
        waddstr_b(w_stats, linkstat.data.tx_bitrate);
    }

    if (linkstat.data.tx_retries) {
        waddstr(w_stats, ", retries: ");
        sprintf(tmp, "%'u", linkstat.data.tx_retries);
        waddstr_b(w_stats, tmp);
    }

    if (linkstat.data.tx_failed) {
        waddstr(w_stats, ", failed: ");
        sprintf(tmp, "%'u", linkstat.data.tx_failed);
        waddstr_b(w_stats, tmp);
    }
    wclrtoborder(w_stats);
    wrefresh(w_stats);
}


int scr_count_loop(WINDOW *w_menu)
{
    time_t now = time(NULL);

    if (!pthread_mutex_trylock(&linkstat.mutex)) {
        display_levels();
        display_stats();
        pthread_mutex_unlock(&linkstat.mutex);
    }

    if (now - last_update >= conf.info_iv) {
        last_update = now;
        display_info(w_if, w_info);
        display_netinfo(w_net);
    }
    return wgetch(w_menu);
}

void scr_count_fini(void)
{
    sampling_stop2();

    delwin(w_net);
    delwin(w_info);
    delwin(w_stats);
    delwin(w_levels);
    delwin(w_if);
}
