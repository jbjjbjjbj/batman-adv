/*
 * Copyright (C) 2007-2012 B.A.T.M.A.N. contributors:
 *
 * Marek Lindner, Simon Wunderlich
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of version 2 of the GNU General Public
 * License as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
 * 02110-1301, USA
 *
 */

#ifndef _NET_BATMAN_ADV_PACKET_H_
#define _NET_BATMAN_ADV_PACKET_H_

#define ETH_P_BATMAN  0x4305	/* unofficial/not registered Ethertype */

enum bat_packettype {
	BAT_IV_OGM		= 0x01,
	BAT_ICMP		= 0x02,
	BAT_UNICAST		= 0x03,
	BAT_BCAST		= 0x04,
	BAT_VIS			= 0x05,
	BAT_UNICAST_FRAG	= 0x06,
	BAT_TT_QUERY		= 0x07,
	BAT_ROAM_ADV		= 0x08,
	BAT_UNICAST_4ADDR	= 0x09
};

enum bat_subtype {
	BAT_P_DATA		= 0x01,
	BAT_P_DAT_DHT_GET	= 0x02,
	BAT_P_DAT_DHT_PUT	= 0x03,
	BAT_P_DAT_CACHE_REPLY	= 0x04
};

/* this file is included by batctl which needs these defines */
#define COMPAT_VERSION 14

enum batman_iv_flags {
	NOT_BEST_NEXT_HOP   = 1 << 3,
	PRIMARIES_FIRST_HOP = 1 << 4,
	VIS_SERVER	    = 1 << 5,
	DIRECTLINK	    = 1 << 6
};

/* ICMP message types */
enum icmp_packettype {
	ECHO_REPLY		= 0,
	DESTINATION_UNREACHABLE = 3,
	ECHO_REQUEST		= 8,
	TTL_EXCEEDED		= 11,
	PARAMETER_PROBLEM	= 12
};

/* vis defines */
enum vis_packettype {
	VIS_TYPE_SERVER_SYNC   = 0,
	VIS_TYPE_CLIENT_UPDATE = 1
};

/* fragmentation defines */
enum unicast_frag_flags {
	UNI_FRAG_HEAD	   = 1 << 0,
	UNI_FRAG_LARGETAIL = 1 << 1
};

/* TT_QUERY subtypes */
#define TT_QUERY_TYPE_MASK 0x3

enum tt_query_packettype {
	TT_REQUEST    = 0,
	TT_RESPONSE   = 1
};

/* TT_QUERY flags */
enum tt_query_flags {
	TT_FULL_TABLE = 1 << 2
};

/* TT_CLIENT flags.
 * Flags from 1 to 1 << 7 are sent on the wire, while flags from 1 << 8 to
 * 1 << 15 are used for local computation only */
enum tt_client_flags {
	TT_CLIENT_DEL     = 1 << 0,
	TT_CLIENT_ROAM    = 1 << 1,
	TT_CLIENT_WIFI    = 1 << 2,
	TT_CLIENT_NOPURGE = 1 << 8,
	TT_CLIENT_NEW     = 1 << 9,
	TT_CLIENT_PENDING = 1 << 10
};

/* claim frame types for the bridge loop avoidance */
enum bla_claimframe {
	CLAIM_TYPE_ADD		= 0x00,
	CLAIM_TYPE_DEL		= 0x01,
	CLAIM_TYPE_ANNOUNCE	= 0x02,
	CLAIM_TYPE_REQUEST	= 0x03
};

/* the destination hardware field in the ARP frame is used to
 * transport the claim type and the group id
 */
struct bla_claim_dst {
	uint8_t magic[3];	/* FF:43:05 */
	uint8_t type;		/* bla_claimframe */
	__be16 group;		/* group id */
} __packed;

struct batman_header {
	uint8_t  packet_type;
	uint8_t  version;  /* batman version field */
	uint8_t  ttl;
} __packed;

struct batman_ogm_packet {
	struct batman_header header;
	uint8_t  flags;    /* 0x40: DIRECTLINK flag, 0x20 VIS_SERVER flag... */
	__be32   seqno;
	uint8_t  orig[ETH_ALEN];
	uint8_t  prev_sender[ETH_ALEN];
	uint8_t  gw_flags;  /* flags related to gateway class */
	uint8_t  tq;
	uint8_t  tt_num_changes;
	uint8_t  ttvn; /* translation table version number */
	__be16   tt_crc;
} __packed;

#define BATMAN_OGM_HLEN sizeof(struct batman_ogm_packet)

struct icmp_packet {
	struct batman_header header;
	uint8_t  msg_type; /* see ICMP message types above */
	uint8_t  dst[ETH_ALEN];
	uint8_t  orig[ETH_ALEN];
	__be16   seqno;
	uint8_t  uid;
	uint8_t  reserved;
} __packed;

#define BAT_RR_LEN 16

/* icmp_packet_rr must start with all fields from imcp_packet
 * as this is assumed by code that handles ICMP packets */
struct icmp_packet_rr {
	struct batman_header header;
	uint8_t  msg_type; /* see ICMP message types above */
	uint8_t  dst[ETH_ALEN];
	uint8_t  orig[ETH_ALEN];
	__be16   seqno;
	uint8_t  uid;
	uint8_t  rr_cur;
	uint8_t  rr[BAT_RR_LEN][ETH_ALEN];
} __packed;

struct unicast_packet {
	struct batman_header header;
	uint8_t  ttvn; /* destination translation table version number */
	uint8_t  dest[ETH_ALEN];
} __packed;

struct unicast_4addr_packet {
	struct unicast_packet u;
	uint8_t src[ETH_ALEN];
	uint8_t subtype;
} __packed;

struct unicast_frag_packet {
	struct batman_header header;
	uint8_t  ttvn; /* destination translation table version number */
	uint8_t  dest[ETH_ALEN];
	uint8_t  flags;
	uint8_t  align;
	uint8_t  orig[ETH_ALEN];
	__be16   seqno;
} __packed;

struct bcast_packet {
	struct batman_header header;
	uint8_t  reserved;
	__be32   seqno;
	uint8_t  orig[ETH_ALEN];
} __packed;

struct vis_packet {
	struct batman_header header;
	uint8_t  vis_type;	 /* which type of vis-participant sent this? */
	__be32   seqno;		 /* sequence number */
	uint8_t  entries;	 /* number of entries behind this struct */
	uint8_t  reserved;
	uint8_t  vis_orig[ETH_ALEN];	/* originator reporting its neighbors */
	uint8_t  target_orig[ETH_ALEN]; /* who should receive this packet */
	uint8_t  sender_orig[ETH_ALEN]; /* who sent or forwarded this packet */
} __packed;

struct tt_query_packet {
	struct batman_header header;
	/* the flag field is a combination of:
	 * - TT_REQUEST or TT_RESPONSE
	 * - TT_FULL_TABLE */
	uint8_t  flags;
	uint8_t  dst[ETH_ALEN];
	uint8_t  src[ETH_ALEN];
	/* the ttvn field is:
	 * if TT_REQUEST: ttvn that triggered the
	 *		  request
	 * if TT_RESPONSE: new ttvn for the src
	 *		   orig_node */
	uint8_t  ttvn;
	/* tt_data field is:
	 * if TT_REQUEST: crc associated with the
	 *		  ttvn
	 * if TT_RESPONSE: table_size */
	__be16   tt_data;
} __packed;

struct roam_adv_packet {
	struct batman_header header;
	uint8_t  reserved;
	uint8_t  dst[ETH_ALEN];
	uint8_t  src[ETH_ALEN];
	uint8_t  client[ETH_ALEN];
} __packed;

struct tt_change {
	uint8_t flags;
	uint8_t addr[ETH_ALEN];
} __packed;

#endif /* _NET_BATMAN_ADV_PACKET_H_ */
