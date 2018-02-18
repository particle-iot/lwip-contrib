/*
 * Copyright (c) 2001-2003 Swedish Institute of Computer Science.
 * All rights reserved. 
 * 
 * Redistribution and use in source and binary forms, with or without modification, 
 * are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice,
 *    this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright notice,
 *    this list of conditions and the following disclaimer in the documentation
 *    and/or other materials provided with the distribution.
 * 3. The name of the author may not be used to endorse or promote products
 *    derived from this software without specific prior written permission. 
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR IMPLIED 
 * WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF 
 * MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT 
 * SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, 
 * EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT 
 * OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS 
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN 
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING 
 * IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY 
 * OF SUCH DAMAGE.
 *
 * This file is part of the lwIP TCP/IP stack.
 * 
 * Author: Adam Dunkels <adam@sics.se>
 *
 */

#include <unistd.h>
#include <fcntl.h>
#include <getopt.h>
#include <string.h>

#include "lwip/opt.h"

#include "lwip/init.h"
#include "lwip/mem.h"
#include "lwip/memp.h"
#include "lwip/sys.h"
#include "lwip/timeouts.h"
#include "lwip/ip_addr.h"
#include "lwip/stats.h"
#include "lwip/tcp.h"
#include "lwip/inet_chksum.h"
#include "lwip/ip_addr.h"

#include "arch/perf.h"

#include "lwip/dns.h"
#include "lwip/dhcp.h"

#include "lwip/tcpip.h"
#include "lwip/sockets.h"

#include "netif/tapif.h"
#include "netif/tunif.h"
#include "netif/unixif.h"
#include "netif/dropif.h"
#include "netif/pcapif.h"
#include "netif/tcpdump.h"

#ifndef LWIP_HAVE_SLIPIF
#define LWIP_HAVE_SLIPIF 0
#endif
#if LWIP_HAVE_SLIPIF
#include "netif/slipif.h"
#define SLIP_PTY_TEST 1
#endif

#include "lwip/apps/httpd.h"
#include "apps/udpecho/udpecho.h"
#include "apps/tcpecho/tcpecho.h"
#include "apps/shell/shell.h"
#include "apps/chargen/chargen.h"
#include "apps/netio/netio.h"
#include "apps/ping/ping.h"
#include "lwip/apps/netbiosns.h"
#include "addons/tcp_isn/tcp_isn.h"

#include "examples/mdns/mdns_example.h"
#include "examples/mqtt/mqtt_example.h"
#include "examples/ppp/pppos_example.h"
#include "examples/snmp/snmp_example.h"
#include "examples/sntp/sntp_example.h"
#include "examples/tftp/tftp_example.h"

#if LWIP_RAW
#include "lwip/icmp.h"
#include "lwip/raw.h"
#endif

#if LWIP_IPV4
/* (manual) host IP configuration */
static ip_addr_t ipaddr, netmask, gw;
#endif /* LWIP_IPV4 */

struct netif netif;

/* ping out destination cmd option */
static unsigned char ping_flag;
static ip_addr_t ping_addr;

/* nonstatic debug cmd option, exported in lwipopts.h */
unsigned char debug_flags;

/** @todo add options for selecting netif, starting DHCP client etc */
static struct option longopts[] = {
  /* turn on debugging output (if build with LWIP_DEBUG) */
  {"debug", no_argument,        NULL, 'd'},
  /* help */
  {"help", no_argument, NULL, 'h'},
#if LWIP_IPV4
  /* gateway address */
  {"gateway", required_argument, NULL, 'g'},
  /* ip address */
  {"ipaddr", required_argument, NULL, 'i'},
  /* netmask */
  {"netmask", required_argument, NULL, 'm'},
  /* ping destination */
  {"ping",   required_argument, NULL, 'p'},
#endif /* LWIP_IPV4 */
  /* new command line options go here! */
  {NULL,   0,                 NULL,  0}
};
#define NUM_OPTS ((sizeof(longopts) / sizeof(struct option)) - 1)

static void init_netifs(void);

static void
usage(void)
{
  unsigned char i;

  printf("options:\n");
  for (i = 0; i < NUM_OPTS; i++) {
    printf("-%c --%s\n",longopts[i].val, longopts[i].name);
  }
}

static void
tcpip_init_done(void *arg)
{
  sys_sem_t *sem;
  sem = (sys_sem_t *)arg;

  init_netifs();
  printf("Network interfaces initialized.\n");

#if LWIP_TCP
  netio_init();
#endif
#if LWIP_TCP && LWIP_NETCONN
  tcpecho_init();
  shell_init();
  httpd_init();
#endif
#if LWIP_UDP && LWIP_NETCONN  
  udpecho_init();
#endif  
#if LWIP_SOCKET
  chargen_init();
#endif
  
#if LWIP_IPV4
  netbiosns_set_name("simhost");
  netbiosns_init();
#endif /* LWIP_IPV4 */

  mdns_example_init();  
  mqtt_example_init();
  snmp_example_init();
  sntp_example_init();
  tftp_example_init();
  
  sys_sem_signal(sem);
}

/*-----------------------------------------------------------------------------------*/
/*-----------------------------------------------------------------------------------*/

#if LWIP_HAVE_SLIPIF
/* (manual) host IP configuration */
#if LWIP_IPV4
static ip_addr_t ipaddr_slip, netmask_slip, gw_slip;
#endif /* LWIP_IPV4 */
struct netif slipif;
#endif /* LWIP_HAVE_SLIPIF */

#if LWIP_NETIF_STATUS_CALLBACK
static void
netif_status_callback(struct netif *nif)
{
  printf("NETIF: %c%c%d is %s\n", nif->name[0], nif->name[1], nif->num,
         netif_is_up(nif) ? "UP" : "DOWN");
#if LWIP_IPV4
  printf("IPV4: Host at %s ", ip4addr_ntoa(netif_ip4_addr(nif)));
  printf("mask %s ", ip4addr_ntoa(netif_ip4_netmask(nif)));
  printf("gateway %s\n", ip4addr_ntoa(netif_ip4_gw(nif)));
#endif /* LWIP_IPV4 */
#if LWIP_IPV6
  printf("IPV6: Host at %s\n", ip6addr_ntoa(netif_ip6_addr(nif, 0)));
#endif /* LWIP_IPV6 */
#if LWIP_NETIF_HOSTNAME
  printf("FQDN: %s\n", netif_get_hostname(nif));
#endif /* LWIP_NETIF_HOSTNAME */
}
#endif /* LWIP_NETIF_STATUS_CALLBACK */

static void
init_netifs(void)
{
#if LWIP_HAVE_SLIPIF
#if SLIP_PTY_TEST
  u8_t siodev_slip = 3;
#else
  u8_t siodev_slip = 0;
#endif

#if LWIP_IPV4
  netif_add(&slipif, ip_2_ip4(&ipaddr_slip), ip_2_ip4(&netmask_slip), ip_2_ip4(&gw_slip),
            LWIP_PTR_NUMERIC_CAST(void*, siodev_slip), slipif_init, tcpip_input);
#else /* LWIP_IPV4 */
  netif_add(&slipif, (void*)&siodev_slip, slipif_init, tcpip_input);
#endif /* LWIP_IPV4 */
#if LWIP_IPV6
  netif_create_ip6_linklocal_address(&slipif, 1);
#endif
#if LWIP_NETIF_STATUS_CALLBACK
  netif_set_status_callback(&slipif, netif_status_callback);
#endif /* LWIP_NETIF_STATUS_CALLBACK */
  netif_set_link_up(&slipif);
  netif_set_up(&slipif);
#endif /* LWIP_HAVE_SLIPIF */

  pppos_example_init();
  
#if LWIP_IPV4
#if LWIP_DHCP
  IP_ADDR4(&gw,      0,0,0,0);
  IP_ADDR4(&ipaddr,  0,0,0,0);
  IP_ADDR4(&netmask, 0,0,0,0);
#endif /* LWIP_DHCP */
  netif_add(&netif, ip_2_ip4(&ipaddr), ip_2_ip4(&netmask), ip_2_ip4(&gw), NULL, tapif_init, tcpip_input);
#else /* LWIP_IPV4 */
  netif_add(&netif, NULL, tapif_init, tcpip_input);
#endif /* LWIP_IPV4 */
#if LWIP_IPV6
  netif_create_ip6_linklocal_address(&netif, 1);
  netif.ip6_autoconfig_enabled = 1;
#endif
#if LWIP_NETIF_STATUS_CALLBACK
  netif_set_status_callback(&netif, netif_status_callback);
#endif /* LWIP_NETIF_STATUS_CALLBACK */
  netif_set_default(&netif);
  netif_set_up(&netif);

#if LWIP_DHCP
  dhcp_start(&netif);
#endif /* LWIP_DHCP */
}

/*-----------------------------------------------------------------------------------*/
int
main(int argc, char **argv)
{
  sys_sem_t sem;
  int ch;
  char ip_str[IPADDR_STRLEN_MAX] = {0};

  /* startup defaults (may be overridden by one or more opts) */
#if LWIP_IPV4
  IP_ADDR4(&gw,      192,168,  0,1);
  IP_ADDR4(&netmask, 255,255,255,0);
  IP_ADDR4(&ipaddr,  192,168,  0,2);
#if LWIP_HAVE_SLIPIF
  IP_ADDR4(&gw_slip,      192,168,  2,  1);
  IP_ADDR4(&netmask_slip, 255,255,255,255);
  IP_ADDR4(&ipaddr_slip,  192,168,  2,  2);
#endif
#endif /* LWIP_IPV4 */
  
  ping_flag = 0;
  /* use debug flags defined by debug.h */
  debug_flags = LWIP_DBG_OFF;
  
  while ((ch = getopt_long(argc, argv, "dhg:i:m:p:", longopts, NULL)) != -1) {
    switch (ch) {
      case 'd':
        debug_flags |= (LWIP_DBG_ON|LWIP_DBG_TRACE|LWIP_DBG_STATE|LWIP_DBG_FRESH|LWIP_DBG_HALT);
        break;
      case 'h':
        usage();
        exit(0);
        break;
#if LWIP_IPV4
      case 'g':
        ipaddr_aton(optarg, &gw);
        break;
      case 'i':
        ipaddr_aton(optarg, &ipaddr);
        break;
      case 'm':
        ipaddr_aton(optarg, &netmask);
        break;
#endif /* LWIP_IPV4 */
      case 'p':
        ping_flag = !0;
        ipaddr_aton(optarg, &ping_addr);
        strncpy(ip_str,ipaddr_ntoa(&ping_addr),sizeof(ip_str));
        ip_str[sizeof(ip_str)-1] = 0; /* ensure \0 termination */
        printf("Using %s to ping\n", ip_str);
        break;
      default:
        usage();
        break;
    }
  }
  argc -= optind;
  argv += optind;
  
#ifdef PERF
  perf_init("/tmp/simhost.perf");
#endif /* PERF */

  printf("System initialized.\n");

  lwip_init_tcp_isn(sys_now(), (u8_t*)&netif);
  
  if(sys_sem_new(&sem, 0) != ERR_OK) {
    LWIP_ASSERT("Failed to create semaphore", 0);
  }
  tcpip_init(tcpip_init_done, &sem);
  sys_sem_wait(&sem);
  printf("TCP/IP initialized.\n");

#if LWIP_SOCKET
  if (ping_flag) {
    ping_init(&ping_addr);
  }
#endif

  printf("Applications started.\n");

#if 0
    stats_display();
#endif

  /* Block forever. */
  pause();
  return 0;
}
/*-----------------------------------------------------------------------------------*/
