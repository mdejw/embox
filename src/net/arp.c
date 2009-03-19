/*
 * arp.c
 *
 *  Created on: Mar 11, 2009
 *      Author: anton
 */
#include "types.h"
#include "common.h"
#include "string.h"
#include "net.h"
#include "arp.h"
#include "net_device.h"
#include "eth.h"
#include "net_pack_manager.h"
#include "net.h"


typedef struct _ARP_ENTITY
{
	unsigned char hw_addr[6];
	unsigned char pw_addr[4];//protocol addr
	void *if_handler;
	unsigned char is_busy;
}ARP_ENTITY;

#define ARP_REQUEST    0x1
#define ARP_RESPONSE   0x2

ARP_ENTITY arp_table[0x100];

#define ARP_TABLE_SIZE (sizeof(arp_table)/sizeof(arp_table[0]))

#define ARP_HARDWARE_TYPE_ETH (unsigned short)0x0001

static inline int find_entity(void *ifdev, unsigned char dst_addr[4])
{
	int i;
	for (i = 0; i < ARP_TABLE_SIZE; i++)
	{
		if(arp_table[i].is_busy && (0 == memcmp(arp_table[i].pw_addr, dst_addr, 4)) && (ifdev==arp_table[i].if_handler))
		{
			return i;
		}
	}
	return -1;
}

static inline int add_entity(void *ifdev, unsigned char ipaddr[4], unsigned char macaddr[6])
{
	int i;
	if (-1 != (i = find_entity(ifdev,ipaddr)))
		return i;

	for (i = 0; i < ARP_TABLE_SIZE; i++)
	{
		if(0 == arp_table[i].is_busy)
		{
			arp_table[i].is_busy = 1;
			arp_table[i].if_handler = ifdev;
			memcpy(arp_table[i].pw_addr, ipaddr, sizeof (arp_table[i].pw_addr));
			memcpy(arp_table[i].hw_addr, macaddr, sizeof(arp_table[i].hw_addr));
			return i;
		}
	}
}

static unsigned char brodcast_mac_addr[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};

static inline net_packet* build_arp_pack(void *ifdev, unsigned char dst_addr[4])
{
	net_packet *pack;
	machdr mach;
	arphdr arph;
	net_device *netdev;
	if (NULL == (pack = net_packet_alloc()))
		return NULL;

	netdev = eth_get_netdevice(ifdev);
//mac header
	memcpy (mach.dst_addr, brodcast_mac_addr, MAC_ADDR_LEN);
	memcpy (mach.src_addr, netdev->hw_addr, MAC_ADDR_LEN);
	mach.type = ARP_PROTOCOL_TYPE;
	mach.raw = NULL;
	pack->mac.mach = &mach;
//arp header
	arph.htype = ARP_HARDWARE_TYPE_ETH;
	arph.ptype = IP_PROTOCOL_TYPE;
	//TODO length hardware and logical type
	arph.hlen = netdev->addr_len;
	arph.plen = 4;
	arph.oper = ARP_REQUEST;
	memcpy (arph.sha, netdev->hw_addr, MAC_ADDR_LEN);
	memcpy (arph.spa, eth_get_ipaddr(ifdev), sizeof(arph.spa));
	memcpy (arph.tpa, eth_get_ipaddr(ifdev), MAC_ADDR_LEN);
	pack->nh.arph = &arph;
	netdev->rebuild_header(pack);
	return pack;
}

//TODO don't like this interface Anton.Bondarev may be arp set field in net_packet and return NULL if error
ARP_RESULT *arp_resolve_addr(void *ifdev, unsigned char dst_addr[4], ARP_RESULT *res)
{
	int i;

	if (NULL == ifdev)
		return NULL;

	if (-1 != (i = find_entity(ifdev, dst_addr)))
	{
		res->hw_dstaddr = arp_table[i].hw_addr;
		res->if_handler = arp_table[i].if_handler;
		return res;
	}
	//send mac packet
	eth_send(build_arp_pack(ifdev, dst_addr));
	sleep(500);
	if (-1 != (i = find_entity(ifdev, dst_addr)))
	{
		res->hw_dstaddr = arp_table[i].hw_addr;
		res->if_handler = arp_table[i].if_handler;
		return res;
	}
	return NULL;
}

int arp_received_packet(net_packet *pack)
{
	net_packet *resp;
	arphdr *arp = pack->nh.arph;



	if(ARP_REQUEST != arp->oper)
		return 0;

	if (0 != memcmp(eth_get_ipaddr(pack->ifdev), arp->tpa, 4))
		return 0;

	TRACE ("arp received packet from ip ");
	ipaddr_print(arp->spa, sizeof(arp->spa));
	TRACE (" mac ");
	macaddr_print(arp->sha, sizeof(arp->spa));

	//add to arp_table
	add_entity(pack->ifdev, arp->spa, arp->sha);

	resp = net_packet_copy(pack);

	memcpy(resp->mac.mach->dst_addr, pack->mac.mach->src_addr, sizeof(resp->mac.mach->dst_addr));
	memcpy(resp->mac.mach->src_addr, pack->netdev->hw_addr, sizeof(resp->mac.mach->src_addr));
	resp->nh.arph->oper = ARP_RESPONSE;
	memcpy(resp->nh.arph->tha, pack->netdev->hw_addr, sizeof(resp->nh.arph->tha));

	eth_send(pack);
}
//TODO delete_interface_from arp table
//TODO delete_targer from arp table
