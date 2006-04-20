/* $Id$ */
/* 
 * Copyright (C) 2003-2006 Benny Prijono <benny@prijono.org>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA 
 */


static const char *desc = 
 " stream.c								\n"
 "									\n"
 " PURPOSE:								\n"
 "  Demonstrate how to use pjmedia stream component to transmit/receive \n"
 "  RTP packets to/from sound device.		    			\n"
 "\n"
 "\n"
 " USAGE:								\n"
 "  stream [options]                                                    \n"
 "\n"
 "\n"
 " Options:\n"
 "  --codec=CODEC         Set the codec name. Valid codec names are:	\n"
 "                        pcma, pcmu, gsm, speexnb, speexwb, speexuwb.	\n"
 "                        (default: pcma).				\n"
 "  --local-port=PORT     Set local RTP port (default=4000)		\n"
 "  --remote=IP:PORT      Set the remote peer. If this option is set,	\n"
 "                        the program will transmit RTP audio to the	\n"
 "                        specified address. (default: recv only)	\n"
 "  --play-file=WAV       Send audio from the WAV file instead of from	\n"
 "                        the sound device.				\n"
// "  --record-file=WAV     Record incoming audio to WAV file instead of	\n"
// "                        playing it to sound device.			\n"
 "  --send-recv           Set stream direction to bidirectional.        \n"
 "  --send-only           Set stream direction to send only		\n"
 "  --recv-only           Set stream direction to recv only (default)   \n"
 "\n"
;



#include <pjlib.h>
#include <pjlib-util.h>
#include <pjmedia.h>
#include <pjmedia-codec.h>

#include <stdlib.h>	/* atoi() */
#include <stdio.h>

#include "util.h"


#define THIS_FILE	"stream.c"


struct codec
{
    char    *name;
    char    *encoding_name;
    int	     pt;
    int	     clock_rate;
} codec[] = 
{
    { "pcma",	  "pcma",  PJMEDIA_RTP_PT_PCMA, 8000 },
    { "pcmu",	  "pcmu",  PJMEDIA_RTP_PT_PCMU, 8000 },
    { "gsm",	  "gsm",   PJMEDIA_RTP_PT_GSM, 8000 },
    { "speexnb",  "speex", 120, 8000 },
    { "speexwb",  "speex", 121, 16000 },
    { "speexuwb", "speex", 122, 32000 },
};


/* Prototype */
static void print_stream_stat(pjmedia_stream *stream);


/* 
 * Register all codecs. 
 */
static pj_status_t init_codecs(pjmedia_endpt *med_endpt)
{
    pj_status_t status;

    status = pjmedia_codec_g711_init(med_endpt);
    PJ_ASSERT_RETURN(status == PJ_SUCCESS, status);

    status = pjmedia_codec_gsm_init(med_endpt);
    PJ_ASSERT_RETURN(status == PJ_SUCCESS, status);

    status = pjmedia_codec_speex_init(med_endpt, 0, -1, -1);
    PJ_ASSERT_RETURN(status == PJ_SUCCESS, status);

    return PJ_SUCCESS;
}


/* 
 * Create stream based on the codec, dir, remote address, etc. 
 */
static pj_status_t create_stream( pj_pool_t *pool,
				  pjmedia_endpt *med_endpt,
				  unsigned codec_index,
				  pjmedia_dir dir,
				  pj_uint16_t local_port,
				  const pj_sockaddr_in *rem_addr,
				  pjmedia_stream **p_stream )
{
    pjmedia_stream_info info;
    pj_status_t status;


    /* Reset stream info. */
    pj_memset(&info, 0, sizeof(info));


    /* Initialize stream info formats */
    info.type = PJMEDIA_TYPE_AUDIO;
    info.dir = dir;
    info.fmt.encoding_name = pj_str(codec[codec_index].encoding_name);
    info.fmt.type = PJMEDIA_TYPE_AUDIO;
    info.fmt.sample_rate = codec[codec_index].clock_rate;
    info.fmt.pt = codec[codec_index].pt;
    info.tx_pt = codec[codec_index].pt;
    info.ssrc = pj_rand();
    

    /* Copy remote address */
    pj_memcpy(&info.rem_addr, rem_addr, sizeof(pj_sockaddr_in));


    /* Create RTP socket */
    status = pj_sock_socket(PJ_AF_INET, PJ_SOCK_DGRAM, 0, 
			    &info.sock_info.rtp_sock);
    PJ_ASSERT_RETURN(status == PJ_SUCCESS, status);


    /* Bind RTP socket to local port */
    info.sock_info.rtp_addr_name.sin_family = PJ_AF_INET;
    info.sock_info.rtp_addr_name.sin_port = pj_htons(local_port);

    status = pj_sock_bind(info.sock_info.rtp_sock, 
			  &info.sock_info.rtp_addr_name,
			  sizeof(pj_sockaddr_in));
    if (status != PJ_SUCCESS) {
	app_perror(THIS_FILE, "Unable to bind RTP socket", status);
	pj_sock_close(info.sock_info.rtp_sock);
	return status;
    }


    /* Create RTCP socket */
    status = pj_sock_socket(PJ_AF_INET, PJ_SOCK_DGRAM, 0,
			    &info.sock_info.rtcp_sock);
    PJ_ASSERT_RETURN(status == PJ_SUCCESS, status);


    /* Bind RTP socket to local port + 1 */
    ++local_port;
    info.sock_info.rtcp_addr_name.sin_family = PJ_AF_INET;
    info.sock_info.rtcp_addr_name.sin_port = pj_htons(local_port);

    status = pj_sock_bind(info.sock_info.rtcp_sock, 
			  &info.sock_info.rtcp_addr_name,
			  sizeof(pj_sockaddr_in));
    if (status != PJ_SUCCESS) {
	app_perror(THIS_FILE, "Unable to bind RTCP socket", status);
	pj_sock_close(info.sock_info.rtp_sock);
	pj_sock_close(info.sock_info.rtcp_sock);
	return status;
    }


    /* Now that the stream info is initialized, we can create the 
     * stream.
     */

    status = pjmedia_stream_create( med_endpt, pool, &info, NULL, p_stream);

    if (status != PJ_SUCCESS) {
	app_perror(THIS_FILE, "Error creating stream", status);
	pj_sock_close(info.sock_info.rtp_sock);
	pj_sock_close(info.sock_info.rtcp_sock);
	return status;
    }


    return PJ_SUCCESS;
}


/*
 * main()
 */
int main(int argc, char *argv[])
{
    pj_caching_pool cp;
    pjmedia_endpt *med_endpt;
    pj_pool_t *pool;
    pjmedia_port *rec_file_port = NULL, *play_file_port = NULL;
    pjmedia_master_port *master_port = NULL;
    pjmedia_snd_port *snd_port = NULL;
    pjmedia_stream *stream = NULL;
    pjmedia_port *stream_port;
    char tmp[10];
    pj_status_t status;


    /* Default values */
    int codec_index = 0;
    pjmedia_dir dir = PJMEDIA_DIR_DECODING;
    pj_sockaddr_in remote_addr;
    pj_uint16_t local_port = 4000;
    char *rec_file = NULL;
    char *play_file = NULL;

    enum {
	OPT_CODEC	= 'c',
	OPT_LOCAL_PORT	= 'p',
	OPT_REMOTE	= 'r',
	OPT_PLAY_FILE	= 'w',
	OPT_RECORD_FILE	= 'R',
	OPT_SEND_RECV	= 'b',
	OPT_SEND_ONLY	= 's',
	OPT_RECV_ONLY	= 'i',
    };

    struct pj_getopt_option long_options[] = {
	{ "codec",	    1, 0, OPT_CODEC },
	{ "local-port",	    1, 0, OPT_LOCAL_PORT },
	{ "remote",	    1, 0, OPT_REMOTE },
	{ "play-file",	    1, 0, OPT_PLAY_FILE },
	{ "record-file",    1, 0, OPT_RECORD_FILE },
	{ "send-recv",      0, 0, OPT_SEND_RECV },
	{ "send-only",      0, 0, OPT_SEND_ONLY },
	{ "recv-only",      0, 0, OPT_RECV_ONLY },
	{ NULL, 0, 0, 0 },
    };

    int c;
    int option_index;


    pj_memset(&remote_addr, 0, sizeof(remote_addr));


    /* init PJLIB : */
    status = pj_init();
    PJ_ASSERT_RETURN(status == PJ_SUCCESS, 1);


    /* Parse arguments */
    pj_optind = 0;
    while((c=pj_getopt_long(argc,argv, "", long_options, &option_index))!=-1) {

	switch (c) {
	case OPT_CODEC:
	    {
		unsigned i;
		for (i=0; i<PJ_ARRAY_SIZE(codec); ++i) {
		    if (pj_ansi_stricmp(pj_optarg, codec[i].name)==0) {
			break;
		    }
		}

		if (i == PJ_ARRAY_SIZE(codec)) {
		    printf("Error: unknown codec %s\n", pj_optarg);
		    return 1;
		}

		codec_index = i;
	    }
	    break;

	case OPT_LOCAL_PORT:
	    local_port = (pj_uint16_t) atoi(pj_optarg);
	    if (local_port < 1) {
		printf("Error: invalid local port %s\n", pj_optarg);
		return 1;
	    }
	    break;

	case OPT_REMOTE:
	    {
		pj_str_t ip = pj_str(strtok(pj_optarg, ":"));
		pj_uint16_t port = (pj_uint16_t) atoi(strtok(NULL, ":"));

		status = pj_sockaddr_in_init(&remote_addr, &ip, port);
		if (status != PJ_SUCCESS) {
		    app_perror(THIS_FILE, "Invalid remote address", status);
		    return 1;
		}
	    }
	    break;

	case OPT_PLAY_FILE:
	    play_file = pj_optarg;
	    break;

	case OPT_RECORD_FILE:
	    rec_file = pj_optarg;
	    break;

	case OPT_SEND_RECV:
	    dir = PJMEDIA_DIR_ENCODING_DECODING;
	    break;

	case OPT_SEND_ONLY:
	    dir = PJMEDIA_DIR_ENCODING;
	    break;

	case OPT_RECV_ONLY:
	    dir = PJMEDIA_DIR_DECODING;
	    break;

	default:
	    printf("Invalid options %s\n", argv[pj_optind]);
	    return 1;
	}

    }


    /* Verify arguments. */
    if (dir & PJMEDIA_DIR_ENCODING) {
	if (remote_addr.sin_addr.s_addr == 0) {
	    printf("Error: remote address must be set\n");
	    return 1;
	}
    }

    if (play_file != NULL && dir != PJMEDIA_DIR_ENCODING) {
	printf("Direction is set to --send-only because of --play-file\n");
	dir = PJMEDIA_DIR_ENCODING;
    }


    /* Must create a pool factory before we can allocate any memory. */
    pj_caching_pool_init(&cp, &pj_pool_factory_default_policy, 0);

    /* 
     * Initialize media endpoint.
     * This will implicitly initialize PJMEDIA too.
     */
    status = pjmedia_endpt_create(&cp.factory, NULL, 1, &med_endpt);
    PJ_ASSERT_RETURN(status == PJ_SUCCESS, 1);

    /* Create memory pool for application purpose */
    pool = pj_pool_create( &cp.factory,	    /* pool factory	    */
			   "app",	    /* pool name.	    */
			   4000,	    /* init size	    */
			   4000,	    /* increment size	    */
			   NULL		    /* callback on error    */
			   );


    /* Register all supported codecs */
    status = init_codecs(med_endpt);
    PJ_ASSERT_RETURN(status == PJ_SUCCESS, 1);


    /* Create stream based on program arguments */
    status = create_stream(pool, med_endpt, codec_index, dir, local_port, 
			   &remote_addr, &stream);
    if (status != PJ_SUCCESS)
	goto on_exit;


    /* Get the port interface of the stream */
    status = pjmedia_stream_get_port( stream, &stream_port);
    PJ_ASSERT_RETURN(status == PJ_SUCCESS, 1);


    if (play_file) {

	status = pjmedia_file_player_port_create(pool, play_file, 0,
						 -1, NULL, &play_file_port);
	if (status != PJ_SUCCESS) {
	    app_perror(THIS_FILE, "Unable to use file", status);
	    goto on_exit;
	}

	status = pjmedia_master_port_create(pool, play_file_port, stream_port,
					    0, &master_port);
	if (status != PJ_SUCCESS) {
	    app_perror(THIS_FILE, "Unable to create master port", status);
	    goto on_exit;
	}

	status = pjmedia_master_port_start(master_port);
	if (status != PJ_SUCCESS) {
	    app_perror(THIS_FILE, "Error starting master port", status);
	    goto on_exit;
	}


    } else {

	/* Create sound device port. */
	if (dir == PJMEDIA_DIR_ENCODING_DECODING)
	    status = pjmedia_snd_port_create(pool, -1, -1, 
					stream_port->info.sample_rate,
					stream_port->info.channel_count,
					stream_port->info.samples_per_frame,
					stream_port->info.bits_per_sample,
					0, &snd_port);
	else if (dir == PJMEDIA_DIR_ENCODING)
	    status = pjmedia_snd_port_create_rec(pool, -1, 
					stream_port->info.sample_rate,
					stream_port->info.channel_count,
					stream_port->info.samples_per_frame,
					stream_port->info.bits_per_sample,
					0, &snd_port);
	else
	    status = pjmedia_snd_port_create_player(pool, -1, 
					stream_port->info.sample_rate,
					stream_port->info.channel_count,
					stream_port->info.samples_per_frame,
					stream_port->info.bits_per_sample,
					0, &snd_port);


	if (status != PJ_SUCCESS) {
	    app_perror(THIS_FILE, "Unable to create sound port", status);
	    goto on_exit;
	}

	/* Connect sound port to stream */
	status = pjmedia_snd_port_connect( snd_port, stream_port );
	PJ_ASSERT_RETURN(status == PJ_SUCCESS, 1);

    }


    /* Done */

    if (dir == PJMEDIA_DIR_DECODING)
	printf("Stream is active, dir is recv-only, local port is %d\n",
	       local_port);
    else if (dir == PJMEDIA_DIR_ENCODING)
	printf("Stream is active, dir is send-only, sending to %s:%d\n",
	       pj_inet_ntoa(remote_addr.sin_addr),
	       pj_ntohs(remote_addr.sin_port));
    else
	printf("Stream is active, send/recv, local port is %d, "
	       "sending to %s:%d\n",
	       local_port,
	       pj_inet_ntoa(remote_addr.sin_addr),
	       pj_ntohs(remote_addr.sin_port));


    for (;;) {

	puts("");
	puts("Commands:");
	puts("  s     Display media statistics");
	puts("  q     Quit");
	puts("");

	printf("Command: "); fflush(stdout);

	fgets(tmp, sizeof(tmp), stdin);

	if (tmp[0] == 's')
	    print_stream_stat(stream);
	else if (tmp[0] == 'q')
	    break;

    }



    /* Start deinitialization: */
on_exit:

    /* Destroy sound device */
    if (snd_port) {
	pjmedia_snd_port_destroy( snd_port );
	PJ_ASSERT_RETURN(status == PJ_SUCCESS, 1);
    }

    /* If there is master port, then we just need to destroy master port
     * (it will recursively destroy upstream and downstream ports, which
     * in this case are file_port and stream_port).
     */
    if (master_port) {
	pjmedia_master_port_destroy(master_port);
	play_file_port = NULL;
	stream = NULL;
    }

    /* Destroy stream */
    if (stream) {
	pjmedia_stream_destroy(stream);
    }

    /* Destroy file ports */
    if (play_file_port)
	pjmedia_port_destroy( play_file_port );
    if (rec_file_port)
	pjmedia_port_destroy( rec_file_port );


    /* Release application pool */
    pj_pool_release( pool );

    /* Destroy media endpoint. */
    pjmedia_endpt_destroy( med_endpt );

    /* Destroy pool factory */
    pj_caching_pool_destroy( &cp );


    return (status == PJ_SUCCESS) ? 0 : 1;
}




static const char *good_number(char *buf, pj_int32_t val)
{
    if (val < 1000) {
	pj_ansi_sprintf(buf, "%d", val);
    } else if (val < 1000000) {
	pj_ansi_sprintf(buf, "%d.%dK", 
			val / 1000,
			(val % 1000) / 100);
    } else {
	pj_ansi_sprintf(buf, "%d.%02dM", 
			val / 1000000,
			(val % 1000000) / 10000);
    }

    return buf;
}



/*
 * Print stream statistics
 */
static void print_stream_stat(pjmedia_stream *stream)
{
    char duration[80], last_update[80];
    char bps[16], ipbps[16], packets[16], bytes[16], ipbytes[16];
    pjmedia_port *port;
    pjmedia_rtcp_stat stat;
    pj_time_val now;


    pj_gettimeofday(&now);
    pjmedia_stream_get_stat(stream, &stat);
    pjmedia_stream_get_port(stream, &port);

    puts("Stream statistics:");

    /* Print duration */
    PJ_TIME_VAL_SUB(now, stat.start);
    sprintf(duration, " Duration: %02ld:%02ld:%02ld.%03ld",
	    now.sec / 3600,
	    (now.sec % 3600) / 60,
	    (now.sec % 60),
	    now.msec);


    printf(" Info: audio %.*s@%dHz, %dms/frame, %sB/s (%sB/s +IP hdr)\n",
   	(int)port->info.encoding_name.slen,
	port->info.encoding_name.ptr,
	port->info.sample_rate,
	port->info.samples_per_frame * 1000 / port->info.sample_rate,
	good_number(bps, port->info.bytes_per_frame * port->info.sample_rate /
		    port->info.sample_rate),
	good_number(ipbps, (port->info.bytes_per_frame+32) * 
			    port->info.sample_rate / port->info.sample_rate));

    if (stat.rx.update_cnt == 0)
	strcpy(last_update, "never");
    else {
	pj_gettimeofday(&now);
	PJ_TIME_VAL_SUB(now, stat.rx.update);
	sprintf(last_update, "%02ldh:%02ldm:%02ld.%03lds ago",
		now.sec / 3600,
		(now.sec % 3600) / 60,
		now.sec % 60,
		now.msec);
    }

    printf(" RX stat last update: %s\n"
	   "    total %s packets %sB received (%sB +IP hdr)%s\n"
	   "    pkt loss=%d (%3.1f%%), dup=%d (%3.1f%%), reorder=%d (%3.1f%%)%s\n"
	   "          (msec)    min     avg     max     last\n"
	   "    loss period: %7.3f %7.3f %7.3f %7.3f%s\n"
	   "    jitter     : %7.3f %7.3f %7.3f %7.3f%s\n",
	   last_update,
	   good_number(packets, stat.rx.pkt),
	   good_number(bytes, stat.rx.bytes),
	   good_number(ipbytes, stat.rx.bytes + stat.rx.pkt * 32),
	   "",
	   stat.rx.loss,
	   stat.rx.loss * 100.0 / stat.rx.pkt,
	   stat.rx.dup, 
	   stat.rx.dup * 100.0 / stat.rx.pkt,
	   stat.rx.reorder, 
	   stat.rx.reorder * 100.0 / stat.rx.pkt,
	   "",
	   stat.rx.loss_period.min / 1000.0, 
	   stat.rx.loss_period.avg / 1000.0, 
	   stat.rx.loss_period.max / 1000.0,
	   stat.rx.loss_period.last / 1000.0,
	   "",
	   stat.rx.jitter.min / 1000.0,
	   stat.rx.jitter.avg / 1000.0,
	   stat.rx.jitter.max / 1000.0,
	   stat.rx.jitter.last / 1000.0,
	   ""
	   );


    if (stat.tx.update_cnt == 0)
	strcpy(last_update, "never");
    else {
	pj_gettimeofday(&now);
	PJ_TIME_VAL_SUB(now, stat.tx.update);
	sprintf(last_update, "%02ldh:%02ldm:%02ld.%03lds ago",
		now.sec / 3600,
		(now.sec % 3600) / 60,
		now.sec % 60,
		now.msec);
    }

    printf(" TX stat last update: %s\n"
	   "    total %s packets %sB sent (%sB +IP hdr)%s\n"
	   "    pkt loss=%d (%3.1f%%), dup=%d (%3.1f%%), reorder=%d (%3.1f%%)%s\n"
	   "          (msec)    min     avg     max     last\n"
	   "    loss period: %7.3f %7.3f %7.3f %7.3f%s\n"
	   "    jitter     : %7.3f %7.3f %7.3f %7.3f%s\n",
	   last_update,
	   good_number(packets, stat.tx.pkt),
	   good_number(bytes, stat.tx.bytes),
	   good_number(ipbytes, stat.tx.bytes + stat.tx.pkt * 32),
	   "",
	   stat.tx.loss,
	   stat.tx.loss * 100.0 / stat.tx.pkt,
	   stat.tx.dup, 
	   stat.tx.dup * 100.0 / stat.tx.pkt,
	   stat.tx.reorder, 
	   stat.tx.reorder * 100.0 / stat.tx.pkt,
	   "",
	   stat.tx.loss_period.min / 1000.0, 
	   stat.tx.loss_period.avg / 1000.0, 
	   stat.tx.loss_period.max / 1000.0,
	   stat.tx.loss_period.last / 1000.0,
	   "",
	   stat.tx.jitter.min / 1000.0,
	   stat.tx.jitter.avg / 1000.0,
	   stat.tx.jitter.max / 1000.0,
	   stat.tx.jitter.last / 1000.0,
	   ""
	   );


    printf(" RTT delay     : %7.3f %7.3f %7.3f %7.3f%s\n", 
	   stat.rtt.min / 1000.0,
	   stat.rtt.avg / 1000.0,
	   stat.rtt.max / 1000.0,
	   stat.rtt.last / 1000.0,
	   ""
	   );

}

