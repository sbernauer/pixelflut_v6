#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <getopt.h>
#include <signal.h>

#include <rte_common.h>
#include <rte_eal.h>
#include <rte_log.h>
#include <rte_lcore.h>
#include <rte_per_lcore.h>
#include <rte_ether.h>
#include <rte_ethdev.h>
#include <rte_mempool.h>
#include <rte_mbuf.h>

#include "network.h"

#define MAX_RX_QUEUE_PER_LCORE 16
#define MAX_TIMER_PERIOD 86400 /* 1 day max */
#define RX_BURST_SIZE 32
#define NB_MBUF (1024 * 8)
#define MBUF_CACHE_SIZE 128

static uint64_t timer_period = 1; /* default period is 1 second */

static volatile bool force_quit;
static uint32_t port_mask = 0;
static unsigned int rx_cores_per_port = 1;
static unsigned int rx_queues_per_core = 1;

struct rte_mempool *mbuf_pool;

/* print usage */
static void
print_usage()
{
    printf("\npixelflut_v6 [EAL options] -- -p PORTMASK\n"
           "  -p PORTMASK: hexadecimal bitmask of ports to configure (for example 0x3 for the first 2 ports)\n"
           "  -c NUMBER: number of cores per port (default is 1)\n"
           "  -q NUMBER: number of queus per core (default is 1)\n"
           "  -T PERIOD: statistics will be refreshed each PERIOD seconds (0 to disable, 1 default, %u maximum)\n", MAX_TIMER_PERIOD);
}

static inline void print_ether_addr(const char *what, struct ether_addr *eth_addr)
{
    char buf[ETHER_ADDR_FMT_SIZE];
    ether_format_addr(buf, ETHER_ADDR_FMT_SIZE, eth_addr);
    printf("%s%s", what, buf);
}

static inline void print_ip6_addr(const char *what, uint8_t *addr) {
    printf("%s %02x%02x:%02x%02x:%02x%02x:%02x%02x:%02x%02x:%02x%02x:%02x%02x:%02x%02x",
        what, addr[0], addr[1], addr[2], addr[3], addr[4], addr[5], addr[6], addr[7],     addr[8], addr[9], addr[10], addr[11], addr[12], addr[13], addr[14], addr[15]);
}

static int
parse_portmask(const char *portmask)
{
    char *end = NULL;
    unsigned long pm;

    /* parse hexadecimal string */
    pm = strtoul(portmask, &end, 16);
    if ((portmask[0] == '\0') || (end == NULL) || (*end != '\0'))
        return -1;

    if (pm == 0)
        return -1;

    return pm;
}

static unsigned int
parse_nqueue(const char *q_arg)
{
    char *end = NULL;
    unsigned long n;

    /* parse hexadecimal string */
    n = strtoul(q_arg, &end, 10);
    if ((q_arg[0] == '\0') || (end == NULL) || (*end != '\0'))
        return 0;
    if (n == 0)
        return 0;
    if (n >= MAX_RX_QUEUE_PER_LCORE)
        return 0;

    return n;
}

static int
parse_timer_period(const char *q_arg)
{
    char *end = NULL;
    int n;

    /* parse number string */
    n = strtol(q_arg, &end, 10);
    if ((q_arg[0] == '\0') || (end == NULL) || (*end != '\0'))
        return -1;
    if (n >= MAX_TIMER_PERIOD)
        return -1;

    return n;
}


/* Parse the argument given in the command line of the application */
static int
parse_args(int argc, char **argv)
{
    int timer_secs;
    int opt;

    // Reset getopt
    optind = 1;

    while ((opt = getopt(argc, argv, "p:c:q:T:w:h:r:f:d?")) != -1) {
        switch (opt) {

    	case('w'):
		case('h'):
		case('r'):
		case('f'):
		case('d'):
			// Parameter that goes to main.c
			break;

        case 'p':
            port_mask = parse_portmask(optarg);
            if (port_mask == 0) {
                printf("invalid portmask\n");
                print_usage();
                return -1;
            }
            break;

        case 'c':
            rx_cores_per_port = parse_nqueue(optarg);
            if (rx_cores_per_port == 0) {
                printf("invalid number of cores per port\n");
                print_usage();
                return -1;
            }
            break;

        case 'q':
            rx_queues_per_core = parse_nqueue(optarg);
            if (rx_queues_per_core == 0) {
                printf("invalid number of queues per core\n");
                print_usage();
                return -1;
            }
            break;

        case 'T':
            timer_secs = parse_timer_period(optarg);
            if (timer_secs < 0) {
                printf("invalid timer period\n");
                print_usage();
                return -1;
            }
            timer_period = timer_secs;
            break;

        default:
            print_usage();
            return -1;
        }
    }

    return 0;
}

static void
signal_handler(int signum)
{
    if (signum == SIGINT || signum == SIGTERM) {
        printf("\n\nSignal %d received, preparing to exit...\n",
                signum);
        force_quit = true;
    }
}

#define CHECK_INTERVAL 100  /* 100ms */
#define MAX_REPEAT_TIMES 10  /* 9s (90 * 100ms) in total */

static void
assert_link_status(unsigned int port_id)
{
    struct rte_eth_link link;
    uint8_t rep_cnt = MAX_REPEAT_TIMES;

    memset(&link, 0, sizeof(link));
    do {
        rte_eth_link_get(port_id, &link);
        if (link.link_status == ETH_LINK_UP)
            break;
        printf(".");
        fflush(stdout);
        rte_delay_ms(CHECK_INTERVAL);
    } while (!force_quit && --rep_cnt);

    if (link.link_status == ETH_LINK_DOWN)
        rte_exit(EXIT_FAILURE, ":: error: link at port %u is still down\n", port_id);
    else
        printf("Port%d Link Up. Speed %u Mbps - %s\n", port_id, link.link_speed, (link.link_duplex == ETH_LINK_FULL_DUPLEX) ? ("full-duplex") : ("half-duplex\n"));
}

static void init_port(unsigned int port_id) {
    int ret;
    uint16_t i;
    struct rte_eth_conf port_conf = {
        .rxmode = {
            .split_hdr_size = 0,
            // .mq_mode = ETH_MQ_RX_RSS,
            .max_rx_pkt_len = ETHER_MAX_LEN,
            // .offloads =
            //  DEV_RX_OFFLOAD_CHECKSUM    |
            //  DEV_RX_OFFLOAD_JUMBO_FRAME |
            //  DEV_RX_OFFLOAD_VLAN_STRIP,
        },
        // .rx_adv_conf = {
        //  .rss_conf = {
        //      .rss_key = NULL,
        //      .rss_hf = ETH_RSS_IP | ETH_RSS_UDP |
        //          ETH_RSS_TCP | ETH_RSS_SCTP,
        //  },
        // },
        // .txmode = {
        //  .offloads =
                // DEV_TX_OFFLOAD_VLAN_INSERT |
                // DEV_TX_OFFLOAD_IPV4_CKSUM  |
                // DEV_TX_OFFLOAD_UDP_CKSUM   |
                // DEV_TX_OFFLOAD_TCP_CKSUM   |
                // DEV_TX_OFFLOAD_SCTP_CKSUM  |
                // DEV_TX_OFFLOAD_TCP_TSO     |
        // },
    };
    struct rte_eth_txconf txq_conf;
    struct rte_eth_rxconf rxq_conf;
    struct rte_eth_dev_info dev_info;

    rte_eth_dev_info_get(port_id, &dev_info);
    port_conf.txmode.offloads &= dev_info.tx_offload_capa;
    printf(":: initializing port: %d\n", port_id);
    printf(":: configuring port: %d\n", port_id);
    ret = rte_eth_dev_configure(port_id, rx_cores_per_port * rx_queues_per_core, 1, &port_conf); // port_id, nb_rx_queue, nb_tx_queue, eth_conf
    if (ret < 0) {
        rte_exit(EXIT_FAILURE, ":: cannot configure device: err=%d, port=%u\n", ret, port_id);
    }

    printf(":: setting up %u RX queues for port: %d\n", rx_cores_per_port * rx_queues_per_core, port_id);
    rxq_conf = dev_info.default_rxconf;
    rxq_conf.offloads = port_conf.rxmode.offloads;

    /* only set Rx queues: something we care only so far */
    for (i = 0; i < rx_cores_per_port * rx_queues_per_core; i++) {
        ret = rte_eth_rx_queue_setup(port_id, i, 512, rte_eth_dev_socket_id(port_id), &rxq_conf, mbuf_pool);
        if (ret < 0) {
            rte_exit(EXIT_FAILURE, ":: Rx queue setup failed: err=%d, port=%u\n", ret, port_id);
        }
    }

    printf(":: setting up 1 TX queue for port: %d\n", port_id);
    txq_conf = dev_info.default_txconf;
    txq_conf.offloads = port_conf.txmode.offloads;

    /* Only one queue */
    for (i = 0; i < 1; i++) {
        ret = rte_eth_tx_queue_setup(port_id, i, 512, rte_eth_dev_socket_id(port_id), &txq_conf);
        if (ret < 0) {
            rte_exit(EXIT_FAILURE, ":: Tx queue setup failed: err=%d, port=%u\n", ret, port_id);
        }
    }

    printf(":: Enabeling promiscuous-mode for port: %d\n", port_id);
    rte_eth_promiscuous_enable(port_id);
    printf(":: Starting port: %d\n", port_id);
    ret = rte_eth_dev_start(port_id);
    if (ret < 0) {
        rte_exit(EXIT_FAILURE, "rte_eth_dev_start:err=%d, port=%u\n", ret, port_id);
    }

    printf(":: Asserting link is up for port: %d\n", port_id);
    assert_link_status(port_id);

    printf(":: initializing port: %d done\n", port_id);
}

void *worker_thread(struct worker_thread_args *args) {
    // Read args
    int port_id = args->port_id;
    int start_queue_id = args->start_queue_id;
    struct fb *fb;

    // get framebuffer on NUMA node
    unsigned numa_node = get_numa_node();
    pthread_mutex_lock(&args->fb_lock);
    fb = fb_get_fb_on_node(args->fb_list, numa_node);
    if(!fb) {
        printf("Failed to find fb on NUMA node %u, creating new fb\n", numa_node);
        if(fb_alloc(&fb, args->fb_size->width, args->fb_size->height)) {
            fprintf(stderr, "Failed to allocate fb on node\n");
            return -1;
        }
        printf("Allocated fb on NUMA node %u\n", fb->numa_node);
        llist_append(args->fb_list, &fb->list);
    }
    pthread_mutex_unlock(&args->fb_lock);

    struct rte_mbuf *mbufs[RX_BURST_SIZE];
    struct ether_hdr *eth_hdr;
    struct ipv4_hdr *ipv4_hdr;
    struct ipv6_hdr *ipv6_hdr;
    struct rte_flow_error error;
    uint16_t nb_rx;
    uint16_t i, queue_id;
    uint16_t x, y;
    uint32_t rgb;

    uint32_t log_counter = 0;

    while (!force_quit) {
//        usleep(100);

        log_counter++;
        if (log_counter > 100000000) {
            struct rte_eth_stats eth_stats;
            /* skip ports that are not enabled */
            rte_eth_stats_get(port_id, &eth_stats);
            printf("Total number of packets for port %u received %lu, dropped rx full %lu and rest= %lu, %lu, %lu\n", port_id, eth_stats.ipackets, eth_stats.imissed, eth_stats.ierrors, eth_stats.rx_nombuf, eth_stats.q_ipackets[0]);
            log_counter = 0;
        }

        for (queue_id = start_queue_id; queue_id < start_queue_id + rx_queues_per_core; queue_id++) {
            nb_rx = rte_eth_rx_burst(port_id, queue_id, mbufs, RX_BURST_SIZE);
            //printf("nb_rx: %u\n", nb_rx);
            if (nb_rx) {
                for (i = 0; i < nb_rx; i++) {
                    struct rte_mbuf *m = mbufs[i];

                    eth_hdr = rte_pktmbuf_mtod(m, struct ether_hdr *);
                    // print_ether_addr("src=", &eth_hdr->s_addr);
                    // print_ether_addr(" - dst=", &eth_hdr->d_addr);
                    // printf(" - queue=0x%x", (unsigned int)i);

                    if (eth_hdr->ether_type == rte_be_to_cpu_16(ETHER_TYPE_IPv6)) {
                        // printf("Found IPv6: ");
                        ipv6_hdr = rte_pktmbuf_mtod_offset(m, struct ipv6_hdr *, sizeof(struct ether_hdr));

                        // if (ipv6_hdr->proto == 58) { // ICMP6
                            //int icmp_type = *(&m + (sizeof(struct ether_hdr)));
                            // uint8_t *icmp_type = rte_pktmbuf_mtod_offset(m, uint8_t*, sizeof(struct ether_hdr) + sizeof(struct ipv6_hdr));
                            // printf("Detected ICMP6 (Type: %u)", *icmp_type);
                            // TODO Reply to ICMP6
                        // }
                        // Continuing without any restriction, client can send whatever type he wants

                        uint8_t *dst = ipv6_hdr->dst_addr;
                        // print_ip6_addr(" IpV6: src: ", ipv6_hdr->src_addr);
                        // print_ip6_addr(" IpV6: dst: ", ipv6_hdr->dst_addr);

                        x = (dst[8] << 8) + dst[9];
                        y = (dst[10] << 8) + dst[11];
                        rgb = (dst[12] << 24) + (dst[13] << 16) + (dst[14] << 8);
                        // printf(" --- x: %d y: %d rgb: %08x ---\n", x, y, rgb);
                        fb_set_pixel(fb, x, y, rgb);

                    // } else if (eth_hdr->ether_type == rte_be_to_cpu_16(ETHER_TYPE_IPv4)) {
                    //     printf("Found IPv4: ");

                    //     ipv4_hdr = rte_pktmbuf_mtod_offset(m, struct ipv4_hdr *, sizeof(struct ether_hdr));
                    //     printf(" IPv4 src: %x dst: %x\n", ipv4_hdr->src_addr, ipv4_hdr->dst_addr);
                    } else {
                        printf("Unkown protocol: %d", eth_hdr->ether_type);
                    }

                    rte_pktmbuf_free(m);
                }
            }
        }
    }


    struct rte_eth_stats eth_stats;
    RTE_ETH_FOREACH_DEV(i) {
        rte_eth_stats_get(i, &eth_stats);
        printf("Total number of packets for port %u received %lu, dropped rx full %lu and rest= %lu, %lu, %lu\n", i, eth_stats.ipackets, eth_stats.imissed, eth_stats.ierrors, eth_stats.rx_nombuf, eth_stats.q_ipackets[0]);
    }


    /* closing and releasing resources */
    rte_flow_flush(port_id, &error);
    rte_eth_dev_stop(port_id);
    rte_eth_dev_close(port_id);

    return NULL;
}

int
net_listen(int argc, char** argv, struct llist* fb_list, pthread_mutex_t fb_lock, struct fb_size* fb_size, bool do_exit)
{
	force_quit = do_exit;
    int ret;
    uint16_t nb_ports, port_id;

    /* init EAL */
    ret = rte_eal_init(argc, argv);
    if (ret < 0)
        rte_exit(EXIT_FAILURE, "Invalid EAL arguments\n");

    argc -= ret;
    argv += ret;

    /* parse application arguments (after the EAL ones) */
    ret = parse_args(argc, argv);
    if (ret < 0)
        rte_exit(EXIT_FAILURE, "Invalid pixelflut_v6 arguments\n");

    printf("DEBUG: port_mask: 0x%x\n", port_mask);
    printf("DEBUG: rx_cores_per_port: %u\n", rx_cores_per_port);
    printf("DEBUG: rx_queues_per_core: %u\n", rx_queues_per_core);
    if (port_mask == 0) {
        printf("==========================\nWARNING: No port enabled, enable them with the option -p\nThere will be no pixels drawn, testing purpose only\n==========================\n");
    }

    /* Check if any port is present */
    nb_ports = rte_eth_dev_count_avail();
    printf("Found %u ports\n", nb_ports);
    // if (nb_ports == 0)
    //     rte_exit(EXIT_FAILURE, "No Ethernet ports - bye\n");

    /* check port mask to possible port mask */
    if (port_mask & ~((1 << nb_ports) - 1))
        rte_exit(EXIT_FAILURE, "Invalid portmask; possible (0x%x)\n",
            (1 << nb_ports) - 1);

    mbuf_pool = rte_pktmbuf_pool_create("mbuf_pool", NB_MBUF, MBUF_CACHE_SIZE, 0, RTE_MBUF_DEFAULT_BUF_SIZE, rte_socket_id());
    if (mbuf_pool == NULL)
        rte_exit(EXIT_FAILURE, "Cannot init mbuf pool\n");

    struct ether_addr mac;
    RTE_ETH_FOREACH_DEV(port_id) {
        /* skip ports that are not enabled */
        if ((port_mask & (1 << port_id)) == 0)
            continue;

        rte_eth_macaddr_get(0, &mac);
        printf("\nPort %u, MAC address: %02X:%02X:%02X:%02X:%02X:%02X\n", port_id, mac.addr_bytes[0], mac.addr_bytes[1], mac.addr_bytes[2], mac.addr_bytes[3], mac.addr_bytes[4], mac.addr_bytes[5]);

        init_port(port_id);
    }

    printf("\nInitialized all ports, launching threads\n");


    unsigned int core_id_counter = 1;
    RTE_ETH_FOREACH_DEV(port_id) {
        /* skip ports that are not enabled */
        if ((port_mask & (1 << port_id)) == 0)
            continue;

        unsigned int queue_id_counter = 0;
        unsigned int i;
        for (i = 0; i < rx_cores_per_port; i++) {
            printf("Launching on port %u, core %u and queue %u - %u (inclusive)\n", port_id, core_id_counter, queue_id_counter, queue_id_counter + rx_queues_per_core - 1);

            struct worker_thread_args args;
            args.port_id = port_id;
            args.start_queue_id = queue_id_counter;
            args.fb_list = fb_list;
            args.fb_lock = fb_lock;
            args.fb_size = fb_size;

            if (!rte_lcore_is_enabled(core_id_counter)) {
                rte_exit(EXIT_FAILURE, "Lcore %u is not enabled, so cant start worker_thread on it. Maybe you have to few cores enables with the EAL-option (-l / -c) or on your system. I need one master core (id: 0) and a lcore for every started worker_thread.\n");
            }

            rte_eal_remote_launch(worker_thread, &args, core_id_counter);
            printf("Launched\n");

            queue_id_counter += rx_queues_per_core;
            core_id_counter++;
        }
    }
    printf("Launched all threads\n");


    // unsigned int core_id_counter;
    // unsigned int queue_id_counter;

    // for (core_id_counter = 0; core_id_counter < nr_cores; core_id_counter++) {


    // for (core_id_counter = 0; core_id_counter < nr_cores; core_id_counter++) {
    //     struct thread_args args;
    //     args.fb=fb;
    //     args.queue_ids = core_id_counter;

    //     rte_eal_remote_launch(dpdk_thread, &args, 3);
    //     core_id_counter++;
    // }

    // while(!force_quit) {
    //     sleep(1);
    // }

    return 0;
}
