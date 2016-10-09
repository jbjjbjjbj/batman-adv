#! /bin/sh

set -e

# for kernel < 3.13 to make netlink compat code work
sed -i \
	-e 's/^static const struct genl_multicast_group batadv_netlink_mcgrps/static __genl_const struct genl_multicast_group batadv_netlink_mcgrps/' \
	build/net/batman-adv/netlink.c
