/*
 * Copyright (C) 2008 B.A.T.M.A.N. contributors:
 * Simon Wunderlich
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

#include "batman-adv-main.h"
#include "batman-adv-send.h"
#include "batman-adv-ttable.h"
#include "batman-adv-vis.h"
#include "batman-adv-log.h"
#include "batman-adv-interface.h"
#include "hash.h"

struct hashtable_t *vis_hash;
DEFINE_SPINLOCK(vis_hash_lock);
static struct vis_info *my_vis_info;
static struct list_head send_list;	/* always locked with vis_hash_lock ... */
static struct timer_list vis_timer;

void free_info(void *data);
void send_vis_packet(struct vis_info *info);
void start_vis_timer(void);


int vis_info_cmp(void *data1, void *data2) {
	struct vis_info *d1, *d2;
	d1 = data1;
	d2 = data2;
	return(memcmp(d1->packet.vis_orig, d2->packet.vis_orig, ETH_ALEN) == 0);
}

/* hashfunction to choose an entry in a hash table of given size */
/* hash algorithm from http://en.wikipedia.org/wiki/Hash_table */
int vis_info_choose(void *data, int size) {
	struct vis_info *vis_info = data;
	unsigned char *key;
	uint32_t hash = 0;
	size_t i;

	key = vis_info->packet.vis_orig;
	for (i = 0; i < ETH_ALEN; i++) {
		hash += key[i];
		hash += (hash << 10);
		hash ^= (hash >> 6);
	}

	hash += (hash << 3);
	hash ^= (hash >> 11);
	hash += (hash << 15);

	return (hash%size);
}

/* try to add the packet to the vis_hash. return NULL if invalid (e.g. too old, broken.. ).
 * vis hash must be locked outside. 
 * is_new is set when the packet is newer than old entries in the hash. */

struct vis_info *add_packet(struct vis_packet *vis_packet, int vis_info_len, int *is_new) 
{
	struct vis_info *info, *old_info;

	*is_new = 0;
	/* sanity check */
	if (vis_hash == NULL)
		return NULL;
	info = kmalloc(sizeof(struct vis_info) + vis_info_len,GFP_KERNEL);
	if (info == NULL) 
		return NULL;

	INIT_LIST_HEAD(&info->send_list);
	info->first_seen = jiffies;
	memcpy(&info->packet, vis_packet, sizeof(struct vis_packet) + vis_info_len);

	/* see if the packet is already in vis_hash */
	old_info = hash_find(vis_hash, info);

	if (old_info != NULL) {
		if (info->packet.seqno - old_info->packet.seqno <= 0) {
			if (old_info->packet.seqno == info->packet.seqno) {
				/* LATER: add to receive_from_list */
				free_info(info);
				return(old_info);
			}
			/* same or newer packet is already in hash. */
			free_info(info);
			return(NULL);
		} 
		/* remove old entry */
		hash_remove(vis_hash, old_info);
		free_info(old_info);
	}

	/* initialize and add new packet. */
	*is_new = 1;

	/* repair if entries is longer than packet. */
	if (info->packet.entries * sizeof(struct vis_info_entry) > vis_info_len) 
		info->packet.entries = vis_info_len / sizeof(struct vis_info);

	/* LATER: fill the list with vis_orig. */
	info->receive_from_list = NULL;

	/* try to add it */
	if (hash_add(vis_hash, info)< 0) {
		/* did not work (for some reason) */
		free_info(info);
		info = NULL;
	}	
	return info;
}

/* handle the server sync packet, forward if needed. */
void receive_server_sync_packet(struct vis_packet *vis_packet, int vis_info_len) 
{
	struct vis_info *info;
	int is_new;

	spin_lock(&vis_hash_lock);
	info = add_packet(vis_packet, vis_info_len, &is_new);
	if (info == NULL)
		goto end;

	/* only if we are server ourselves and packet is newer than the one in hash.*/
	if ((my_vis_info->packet.vis_type == VIS_TYPE_SERVER_SYNC) && is_new) {
		/* TODO: remove it first? */
		list_add_tail(&info->send_list, &send_list);
	}

end:
	spin_unlock(&vis_hash_lock);

}

/* handle an incoming client update packet and schedule forward if needed. */
void receive_client_update_packet(struct vis_packet *vis_packet, int vis_info_len) 
{
	struct vis_info *info;
	int is_new;
	/* clients shall not broadcast. */
	if (is_bcast(vis_packet->target_orig)) 
		return;

	spin_lock(&vis_hash_lock);
	info = add_packet(vis_packet, vis_info_len, &is_new);
	if (info == NULL)
		goto end;


	/* send only if we're the target server or ... */
	if (my_vis_info->packet.vis_type == VIS_TYPE_SERVER_SYNC
		&& is_my_mac(info->packet.target_orig)
		&& is_new) {

		info->packet.vis_type = VIS_TYPE_SERVER_SYNC;	/* upgrade! */
		list_add_tail(&info->send_list, &send_list);

	 /* ... we're not the recipient (and thus need to forward). */
	} else if (!is_my_mac(info->packet.target_orig))
		list_add_tail(&info->send_list, &send_list);

end:
	spin_unlock(&vis_hash_lock);
}


/* send own vis data */
static void generate_vis_packet(void) 
{
	struct hash_it_t *hashit = NULL;
	struct orig_node *orig_node;
	struct vis_info *info = (struct vis_info *)my_vis_info;
	struct vis_info_entry *entry, *entry_array;
	struct hna_local_entry *hna_local_entry;
	int best_tq = -1;

	info->first_seen = jiffies;

	spin_lock(&orig_hash_lock);
	memcpy(info->packet.target_orig, broadcastAddr, 6);
	info->packet.ttl = TTL;
	info->packet.seqno++;
	info->packet.entries = 0;

	if (my_vis_info->packet.vis_type == VIS_TYPE_CLIENT_UPDATE) {
		/* find vis-server to receive. */
		while (NULL != (hashit = hash_iterate(orig_hash, hashit))) {
			orig_node = hashit->bucket->data;
			if ((orig_node != NULL) && (orig_node->router == NULL)) {
				if ((orig_node->flags & VIS_SERVER) && orig_node->router->tq_avg > best_tq) {
					best_tq = orig_node->router->tq_avg;
					memcpy(info->packet.target_orig, orig_node->orig,6);
				}
			}
		}
		if (best_tq < 0) {
			info->packet.ttl = 0;	/* don't send */
			spin_unlock(&orig_hash_lock);
			return;
		}
	}
	hashit = NULL;

	entry_array = (struct vis_info_entry *)((char *)info + sizeof(struct vis_info));

	while (NULL != (hashit = hash_iterate(orig_hash, hashit))) {
		orig_node = hashit->bucket->data;
		if (orig_node->router != NULL
			&& memcmp(orig_node->router->addr, orig_node->orig, ETH_ALEN) == 0
			&& orig_node->router->tq_avg > 0) {

			/* fill one entry into buffer. */
			entry = &entry_array[info->packet.entries];

			memcpy(entry->dest, orig_node->orig, ETH_ALEN);
			entry->quality = orig_node->router->tq_avg;
			info->packet.entries++;
			/* don't fill more than 1000 bytes */
			if (info->packet.entries + 1 > (1000 - sizeof(struct vis_info)) / sizeof(struct vis_info_entry)) {
				spin_unlock(&orig_hash_lock);
				return;
			}
		}
	}

	spin_unlock(&orig_hash_lock);

	hashit = NULL;
	spin_lock(&hna_local_hash_lock);
	while (NULL != (hashit = hash_iterate(hna_local_hash, hashit))) {
		hna_local_entry = hashit->bucket->data;

		entry = &entry_array[info->packet.entries];

		memcpy(entry->dest, hna_local_entry->addr, ETH_ALEN);
		entry->quality = 0; /* 0 means HNA */
		info->packet.entries++;

		/* don't fill more than 1000 bytes */
		if (info->packet.entries + 1 > (1000 - sizeof(struct vis_info)) / sizeof(struct vis_info_entry)) {
			spin_unlock(&hna_local_hash_lock);
			return;
		}
	}
	spin_unlock(&hna_local_hash_lock);

}
void purge_vis_packets(void)
{
	struct hash_it_t *hashit = NULL;
	struct vis_info *info;

	spin_lock(&vis_hash_lock);
	while (NULL != (hashit = hash_iterate(vis_hash, hashit))) {
		info = hashit->bucket->data;
		if (info == my_vis_info)	/* never purge own data. */
			continue;
		if (time_after(jiffies, info->first_seen + (VIS_TIMEOUT/1000)*HZ)) {
			hash_remove_bucket(vis_hash, hashit);
			free_info(info);
		}
	}
	spin_unlock(&vis_hash_lock);
}

/* called from timer; send (and maybe generate) vis packet. */
void send_vis_packets(unsigned long arg) 
{
	struct vis_info *info, *temp;
	debug_log(LOG_TYPE_NOTICE, "Sending vis packets...\n");

	purge_vis_packets();
	spin_lock(&vis_hash_lock);
	generate_vis_packet();
	
	list_add_tail(&my_vis_info->send_list, &send_list);

	list_for_each_entry_safe(info, temp, &send_list, send_list) {
		list_del_init(&info->send_list);
		send_vis_packet(info);
	}
	spin_unlock(&vis_hash_lock);
	start_vis_timer();
	

	debug_log(LOG_TYPE_NOTICE, "Sending vis packets done...\n");
}

/* only send one vis packet. called from send_vis_packets() */
void send_vis_packet(struct vis_info *info) {
	struct batman_if *batman_if;
	struct orig_node *orig_node;
	int packet_length;


	if (info->packet.ttl < 2) {
		debug_log(LOG_TYPE_NOTICE, "Error - can't send vis packet: ttl exceeded\n");
		return;
	}

	/* LATER: change this to send only to vis "neighbours" */
	if (info->packet.vis_type == VIS_TYPE_SERVER_SYNC)
		memcpy(info->packet.target_orig, broadcastAddr, ETH_ALEN);

	memcpy(info->packet.sender_orig, mainIfAddr, ETH_ALEN);

	info->packet.ttl--;

	packet_length = sizeof(struct vis_packet) + info->packet.entries * sizeof(struct vis_info_entry);

	if (is_bcast(info->packet.target_orig)) {
		/* broadcast packet */
		rcu_read_lock();
		list_for_each_entry_rcu(batman_if, &if_list, list) {
			send_raw_packet((unsigned char *)&info->packet, packet_length, 
					batman_if->net_dev->dev_addr, broadcastAddr, batman_if);
		}
		rcu_read_unlock();
	} else {
		/* unicast packet */
		spin_lock(&orig_hash_lock);
		orig_node = ((struct orig_node *) hash_find(orig_hash, info->packet.target_orig));

		if ((orig_node != NULL) && (orig_node->batman_if != NULL) && (orig_node->router != NULL)) {
			send_raw_packet((unsigned char *) &info->packet, packet_length, 
					orig_node->batman_if->net_dev->dev_addr, orig_node->router->addr, orig_node->batman_if);
		}
		spin_unlock(&orig_hash_lock);
		
	}
	info->packet.ttl++; /* restore TTL */
}

/* receive and handle a vis packet */
void receive_vis_packet(struct ethhdr *ethhdr, struct vis_packet *vis_packet, int vis_info_len)
{
	/* ignore own packets */
	if (is_my_mac(vis_packet->vis_orig))
		return;
	if (is_my_mac(vis_packet->sender_orig))
		return;

	switch (vis_packet->vis_type) {
	case VIS_TYPE_SERVER_SYNC:
		receive_server_sync_packet(vis_packet, vis_info_len);
		break;

	case VIS_TYPE_CLIENT_UPDATE:
		receive_client_update_packet(vis_packet, vis_info_len);
		break;

	default:	/* ignore unknown packet */
		break;
	}
}

/* init the vis server. this may only be called when if_list is already initialized
 * (e.g. bat0 is initialized, interfaces have been added) */
int vis_init(void) 
{
	vis_hash = hash_new(256, vis_info_cmp, vis_info_choose);
	if (vis_hash == NULL) {
		debug_log(LOG_TYPE_CRIT, "Can't initialize vis_hash\n");
		return(-1);
	}

	my_vis_info = kmalloc(1000, GFP_KERNEL);
	if (my_vis_info == NULL) {
		vis_quit();
		debug_log(LOG_TYPE_CRIT, "Can't initialize vis packet\n");
		return(-1);
	}
	/* prefill the vis info */
	my_vis_info->first_seen = jiffies - atomic_read(&vis_interval);
	my_vis_info->receive_from_list = NULL; /* LATER */
	my_vis_info->packet.packet_type = BAT_VIS;
	my_vis_info->packet.vis_type = VIS_TYPE_SERVER_SYNC;
	my_vis_info->packet.ttl = TTL;
	my_vis_info->packet.seqno = 0;
	my_vis_info->packet.entries = 0;

	INIT_LIST_HEAD(&send_list);

	memcpy(my_vis_info->packet.vis_orig, mainIfAddr, ETH_ALEN);
	memcpy(my_vis_info->packet.sender_orig, mainIfAddr, ETH_ALEN);

	if (hash_add(vis_hash, my_vis_info) < 0) {
		debug_log(LOG_TYPE_CRIT, "Can't add own vis packet into hash\n");
		free_info(my_vis_info);	/* not in hash, need to remove it manually. */
		vis_quit();
		return(-1);
	}

	start_vis_timer();
	return(0);
}

/* free the info */
void free_info(void *data)
{
	struct vis_info *info = data;
	list_del_init(&info->send_list);
	kfree(info);
}

/* shutdown vis-server */
int vis_quit(void) 
{
	spin_lock(&vis_hash_lock);
	del_timer_sync(&vis_timer);
	if (vis_hash != NULL)
		hash_delete(vis_hash, free_info);	/* properly remove, kill timers ... */
	vis_hash = NULL;
	my_vis_info = NULL;
	spin_unlock(&vis_hash_lock);
	return(0);
}

/* schedule own packet for (re)transmission */
void start_vis_timer(void)
{
	init_timer(&vis_timer);

	vis_timer.expires = jiffies + (atomic_read(&vis_interval)/1000) * HZ;
	vis_timer.data = 0;
	vis_timer.function = send_vis_packets;

	add_timer(&vis_timer);
}

