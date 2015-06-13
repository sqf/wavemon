/*
 * PROTOTYPE: add nl80211 calls to iw_if. Mostly copied/stolen from iw
 */
#include "wavemon.h"
#include <net/if.h>
#include <errno.h>
#include <err.h>
#include <stdio.h>
#include <stdlib.h>

#include "iw_nl80211.h"

/* Initialize and allocate the nl80211 sub-structure */
struct iw_nl80211_stat *iw_nl80211_init(void)
{
	struct iw_nl80211_stat *is = calloc(1, sizeof(*is));

	if (is == NULL)
		err(1, "failed to allocate nl80211 structure");

	is->nl_sock = nl_socket_alloc();
	if (!is->nl_sock)
		err(1, "Failed to allocate netlink socket");

	/* Set rx/tx socket buffer size to 8kb (default is 32kb) */
	nl_socket_set_buffer_size(is->nl_sock, 8192, 8192);

	if (genl_connect(is->nl_sock))
		err(1, "failed to connect to GeNetlink");

	is->nl80211_id = genl_ctrl_resolve(is->nl_sock, "nl80211");
	if (is->nl80211_id < 0)
		err(1, "nl80211 not found");

	is->ifindex = if_nametoindex(conf_ifname());
	if (is->ifindex < 0)
		err(1, "failed to look up interface %s", conf_ifname());

	return is;
}

/* Teardown */
void iw_nl80211_fini(struct iw_nl80211_stat **isptr)
{
	nl_socket_free((*isptr)->nl_sock);
	free(*isptr);
	*isptr = NULL;
}

///////////////////////////////////////////////////////////////////////////////////////////////
// COMMAND HANDLING
///////////////////////////////////////////////////////////////////////////////////////////////

// Predefined handlers, stolen from iw:iw.c
static int error_handler(struct sockaddr_nl *nla, struct nlmsgerr *err,
			 void *arg)
{
	int *ret = arg;
	*ret = err->error;
	return NL_STOP;
}

static int finish_handler(struct nl_msg *msg, void *arg)
{
	int *ret = arg;
	*ret = 0;
	return NL_SKIP;
}

static int ack_handler(struct nl_msg *msg, void *arg)
{
	int *ret = arg;
	*ret = 0;
	return NL_STOP;
}

// stolen from iw:station.c
void parse_bitrate(struct nlattr *bitrate_attr, char *buf, int buflen)
{
	int rate = 0;
	char *pos = buf;
	struct nlattr *rinfo[NL80211_RATE_INFO_MAX + 1];
	static struct nla_policy rate_policy[NL80211_RATE_INFO_MAX + 1] = {
		[NL80211_RATE_INFO_BITRATE] = { .type = NLA_U16 },
		[NL80211_RATE_INFO_BITRATE32] = { .type = NLA_U32 },
		[NL80211_RATE_INFO_MCS] = { .type = NLA_U8 },
		[NL80211_RATE_INFO_40_MHZ_WIDTH] = { .type = NLA_FLAG },
		[NL80211_RATE_INFO_SHORT_GI] = { .type = NLA_FLAG },
	};

	if (nla_parse_nested(rinfo, NL80211_RATE_INFO_MAX,
			     bitrate_attr, rate_policy)) {
		snprintf(buf, buflen, "failed to parse nested rate attributes!");
		return;
	}

	if (rinfo[NL80211_RATE_INFO_BITRATE32])
		rate = nla_get_u32(rinfo[NL80211_RATE_INFO_BITRATE32]);
	else if (rinfo[NL80211_RATE_INFO_BITRATE])
		rate = nla_get_u16(rinfo[NL80211_RATE_INFO_BITRATE]);
	if (rate > 0)
		pos += snprintf(pos, buflen - (pos - buf),
				"%d.%d MBit/s", rate / 10, rate % 10);

	if (rinfo[NL80211_RATE_INFO_MCS])
		pos += snprintf(pos, buflen - (pos - buf),
				" MCS %d", nla_get_u8(rinfo[NL80211_RATE_INFO_MCS]));
	if (rinfo[NL80211_RATE_INFO_VHT_MCS])
		pos += snprintf(pos, buflen - (pos - buf),
				" VHT-MCS %d", nla_get_u8(rinfo[NL80211_RATE_INFO_VHT_MCS]));
	if (rinfo[NL80211_RATE_INFO_40_MHZ_WIDTH])
		pos += snprintf(pos, buflen - (pos - buf), " 40MHz");
	if (rinfo[NL80211_RATE_INFO_80_MHZ_WIDTH])
		pos += snprintf(pos, buflen - (pos - buf), " 80MHz");
	if (rinfo[NL80211_RATE_INFO_80P80_MHZ_WIDTH])
		pos += snprintf(pos, buflen - (pos - buf), " 80P80MHz");
	if (rinfo[NL80211_RATE_INFO_160_MHZ_WIDTH])
		pos += snprintf(pos, buflen - (pos - buf), " 160MHz");
	if (rinfo[NL80211_RATE_INFO_SHORT_GI])
		pos += snprintf(pos, buflen - (pos - buf), " short GI");
	if (rinfo[NL80211_RATE_INFO_VHT_NSS])
		pos += snprintf(pos, buflen - (pos - buf),
				" VHT-NSS %d", nla_get_u8(rinfo[NL80211_RATE_INFO_VHT_NSS]));
}

// stolen and modified from iw:station.c
static int print_sta_handler(struct nl_msg *msg, void *arg)
{
	struct iw_nl80211_stat *is = (struct iw_nl80211_stat *)arg;

	struct nlattr *tb[NL80211_ATTR_MAX + 1];
	struct genlmsghdr *gnlh = nlmsg_data(nlmsg_hdr(msg));
	struct nlattr *sinfo[NL80211_STA_INFO_MAX + 1];
	char state_name[10];
	struct nl80211_sta_flag_update *sta_flags;
	static struct nla_policy stats_policy[NL80211_STA_INFO_MAX + 1] = {
		[NL80211_STA_INFO_INACTIVE_TIME] = { .type = NLA_U32 },
		[NL80211_STA_INFO_RX_BYTES] = { .type = NLA_U32 },
		[NL80211_STA_INFO_TX_BYTES] = { .type = NLA_U32 },
		[NL80211_STA_INFO_RX_PACKETS] = { .type = NLA_U32 },
		[NL80211_STA_INFO_TX_PACKETS] = { .type = NLA_U32 },
		[NL80211_STA_INFO_SIGNAL] = { .type = NLA_U8 },
		[NL80211_STA_INFO_T_OFFSET] = { .type = NLA_U64 },
		[NL80211_STA_INFO_TX_BITRATE] = { .type = NLA_NESTED },
		[NL80211_STA_INFO_RX_BITRATE] = { .type = NLA_NESTED },
		[NL80211_STA_INFO_LLID] = { .type = NLA_U16 },
		[NL80211_STA_INFO_PLID] = { .type = NLA_U16 },
		[NL80211_STA_INFO_PLINK_STATE] = { .type = NLA_U8 },
		[NL80211_STA_INFO_TX_RETRIES] = { .type = NLA_U32 },
		[NL80211_STA_INFO_TX_FAILED] = { .type = NLA_U32 },
		[NL80211_STA_INFO_STA_FLAGS] =
			{ .minlen = sizeof(struct nl80211_sta_flag_update) },
		[NL80211_STA_INFO_LOCAL_PM] = { .type = NLA_U32},
		[NL80211_STA_INFO_PEER_PM] = { .type = NLA_U32},
		[NL80211_STA_INFO_NONPEER_PM] = { .type = NLA_U32},
		[NL80211_STA_INFO_CHAIN_SIGNAL] = { .type = NLA_NESTED },
		[NL80211_STA_INFO_CHAIN_SIGNAL_AVG] = { .type = NLA_NESTED },
	};

	if (!is)
		err_quit("is is NULL");
	nla_parse(tb, NL80211_ATTR_MAX, genlmsg_attrdata(gnlh, 0),
		  genlmsg_attrlen(gnlh, 0), NULL);

	/*
	 * TODO: validate the interface and mac address!
	 * Otherwise, there's a race condition as soon as
	 * the kernel starts sending station notifications.
	 */

	if (!tb[NL80211_ATTR_STA_INFO]) {
		fprintf(stderr, "sta stats missing!\n");
		return NL_SKIP;
	}
	if (nla_parse_nested(sinfo, NL80211_STA_INFO_MAX,
			     tb[NL80211_ATTR_STA_INFO],
			     stats_policy)) {
		fprintf(stderr, "failed to parse nested attributes!\n");
		return NL_SKIP;
	}

	memcpy(&is->mac_addr, nla_data(tb[NL80211_ATTR_MAC]), ETH_ALEN);

	if (sinfo[NL80211_STA_INFO_INACTIVE_TIME])
		is->inactive_time = nla_get_u32(sinfo[NL80211_STA_INFO_INACTIVE_TIME]);
	if (sinfo[NL80211_STA_INFO_RX_BYTES])
		is->rx_bytes = nla_get_u32(sinfo[NL80211_STA_INFO_RX_BYTES]);
	if (sinfo[NL80211_STA_INFO_RX_PACKETS])
		is->rx_packets = nla_get_u32(sinfo[NL80211_STA_INFO_RX_PACKETS]);
	if (sinfo[NL80211_STA_INFO_TX_BYTES])
		is->tx_packets = nla_get_u32(sinfo[NL80211_STA_INFO_TX_BYTES]);
	if (sinfo[NL80211_STA_INFO_TX_PACKETS])
		is->tx_bytes = nla_get_u32(sinfo[NL80211_STA_INFO_TX_PACKETS]);
	if (sinfo[NL80211_STA_INFO_TX_RETRIES])
		is->tx_retries = nla_get_u32(sinfo[NL80211_STA_INFO_TX_RETRIES]);
	if (sinfo[NL80211_STA_INFO_TX_FAILED])
		is->tx_failed = nla_get_u32(sinfo[NL80211_STA_INFO_TX_FAILED]);

	/* XXX
	char *chain;
	chain = get_chain_signal(sinfo[NL80211_STA_INFO_CHAIN_SIGNAL]);
	if (sinfo[NL80211_STA_INFO_SIGNAL])
		printf("\n\tsignal:  \t%d %sdBm",
			(int8_t)nla_get_u8(sinfo[NL80211_STA_INFO_SIGNAL]),
			chain);

	chain = get_chain_signal(sinfo[NL80211_STA_INFO_CHAIN_SIGNAL_AVG]);
	if (sinfo[NL80211_STA_INFO_SIGNAL_AVG])
		printf("\n\tsignal avg:\t%d %sdBm",
			(int8_t)nla_get_u8(sinfo[NL80211_STA_INFO_SIGNAL_AVG]),
			chain);
			*/

	if (sinfo[NL80211_STA_INFO_T_OFFSET])
		is->tx_offset = nla_get_u64(sinfo[NL80211_STA_INFO_T_OFFSET]);

	if (sinfo[NL80211_STA_INFO_TX_BITRATE])
		parse_bitrate(sinfo[NL80211_STA_INFO_TX_BITRATE], is->tx_bitrate, sizeof(is->tx_bitrate));

	if (sinfo[NL80211_STA_INFO_RX_BITRATE])
		parse_bitrate(sinfo[NL80211_STA_INFO_RX_BITRATE], is->rx_bitrate, sizeof(is->rx_bitrate));

	if (sinfo[NL80211_STA_INFO_EXPECTED_THROUGHPUT]) {
		is->expected_thru = nla_get_u32(sinfo[NL80211_STA_INFO_EXPECTED_THROUGHPUT]);
		/* convert in Mbps but scale by 1000 to save kbps units */
		is->expected_thru = is->expected_thru * 1000 / 1024;
	}

	if (sinfo[NL80211_STA_INFO_LLID])
		printf("\n\tmesh llid:\t%d",
			nla_get_u16(sinfo[NL80211_STA_INFO_LLID]));
	if (sinfo[NL80211_STA_INFO_PLID])
		printf("\n\tmesh plid:\t%d",
			nla_get_u16(sinfo[NL80211_STA_INFO_PLID]));
	if (sinfo[NL80211_STA_INFO_PLINK_STATE]) {
		switch (nla_get_u8(sinfo[NL80211_STA_INFO_PLINK_STATE])) {
		case LISTEN:
			strcpy(state_name, "LISTEN");
			break;
		case OPN_SNT:
			strcpy(state_name, "OPN_SNT");
			break;
		case OPN_RCVD:
			strcpy(state_name, "OPN_RCVD");
			break;
		case CNF_RCVD:
			strcpy(state_name, "CNF_RCVD");
			break;
		case ESTAB:
			strcpy(state_name, "ESTAB");
			break;
		case HOLDING:
			strcpy(state_name, "HOLDING");
			break;
		case BLOCKED:
			strcpy(state_name, "BLOCKED");
			break;
		default:
			strcpy(state_name, "UNKNOWN");
			break;
		}
		printf("\n\tmesh plink:\t%s", state_name);
	}

	/* XXX
	if (sinfo[NL80211_STA_INFO_LOCAL_PM]) {
		printf("\n\tmesh local PS mode:\t");
		print_power_mode(sinfo[NL80211_STA_INFO_LOCAL_PM]);
	}
	if (sinfo[NL80211_STA_INFO_PEER_PM]) {
		printf("\n\tmesh peer PS mode:\t");
		print_power_mode(sinfo[NL80211_STA_INFO_PEER_PM]);
	}
	if (sinfo[NL80211_STA_INFO_NONPEER_PM]) {
		printf("\n\tmesh non-peer PS mode:\t");
		print_power_mode(sinfo[NL80211_STA_INFO_NONPEER_PM]);
	}
	*/

	if (sinfo[NL80211_STA_INFO_STA_FLAGS]) {
		sta_flags = (struct nl80211_sta_flag_update *)
			    nla_data(sinfo[NL80211_STA_INFO_STA_FLAGS]);

		if (sta_flags->mask & BIT(NL80211_STA_FLAG_AUTHORIZED) &&
		    sta_flags->set & BIT(NL80211_STA_FLAG_AUTHORIZED))
			is->authenticated = true;

		if (sta_flags->mask & BIT(NL80211_STA_FLAG_AUTHENTICATED) &&
		    sta_flags->set & BIT(NL80211_STA_FLAG_AUTHENTICATED))
			is->authenticated = true;

		if (sta_flags->mask & BIT(NL80211_STA_FLAG_SHORT_PREAMBLE) &&
		    sta_flags->set & BIT(NL80211_STA_FLAG_SHORT_PREAMBLE))
			is->long_preamble = true;

		if (sta_flags->mask & BIT(NL80211_STA_FLAG_WME) &&
		    sta_flags->set & BIT(NL80211_STA_FLAG_WME))
			is->wme = true;

		if (sta_flags->mask & BIT(NL80211_STA_FLAG_MFP) &&
		    sta_flags->set & BIT(NL80211_STA_FLAG_MFP))
			is->mfp = true;

		if (sta_flags->mask & BIT(NL80211_STA_FLAG_TDLS_PEER) &&
		    sta_flags->set & BIT(NL80211_STA_FLAG_TDLS_PEER))
			is->tdls = true;
	}

	return NL_SKIP;
}


/* stolen/modified from iw:iw.c */
int handle_cmd(struct iw_nl80211_stat *is, struct cmd *cmd)
{
	struct nl_cb *cb;
	struct nl_msg *msg;
	int ret;

	/*-------------------------------------------------------------------------
	 * Send 'station dump' message
	 *-------------------------------------------------------------------------*/
	msg = nlmsg_alloc();
	if (!msg)
		err(2, "failed to allocate netlink message");

	cb = nl_cb_alloc(0 ? NL_CB_DEBUG : NL_CB_DEFAULT);
	if (!cb)
		err(2, "failed to allocate netlink callback");

	genlmsg_put(msg, 0, 0, is->nl80211_id, 0, cmd->flags, cmd->cmd, 0);

	/* netdev identifier: interface index */
	if (nla_put(msg, NL80211_ATTR_IFINDEX, sizeof(is->ifindex), &is->ifindex) < 0)
		err(2, "failed to add ifindex attribute to netlink message");

	// wdev identifier: wdev index
	// NLA_PUT_U64(msg, NL80211_ATTR_WDEV, devidx);

	/* Set callback for this message */
	nl_cb_set(cb, NL_CB_VALID, NL_CB_CUSTOM, cmd->handler, cmd->handler_arg);

	ret = nl_send_auto_complete(is->nl_sock, msg);
	if (ret < 0)
		err(2, "failed to send station-dump message");

	/*-------------------------------------------------------------------------
	 * Receive loop
	 *-------------------------------------------------------------------------*/
	nl_cb_err(cb, NL_CB_CUSTOM, error_handler, &ret);
	nl_cb_set(cb, NL_CB_FINISH, NL_CB_CUSTOM, finish_handler, &ret);
	nl_cb_set(cb, NL_CB_ACK, NL_CB_CUSTOM, ack_handler, &ret);

	while (ret > 0)
		nl_recvmsgs(is->nl_sock, cb);

	nl_cb_put(cb);
	nlmsg_free(msg);

	return 0;
}

// Populate statistics
void iw_nl80211_getstat(struct iw_nl80211_stat *is)
{
	struct cmd cmd = {
		.cmd		= NL80211_CMD_GET_STATION,
		.flags		= NLM_F_DUMP,
		.handler	= print_sta_handler,
		.handler_arg	= is
	};

	handle_cmd(is, &cmd);
}
