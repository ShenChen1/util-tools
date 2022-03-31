#include <arpa/inet.h>
#include <getopt.h>
#include <linux/if_packet.h>
#include <net/if.h>
#include <netinet/ether.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <unistd.h>

typedef struct {
    char interface[IFNAMSIZ];
    int interval;
    int packetsize;
    int count;
    uint32_t ether_mac[ETH_ALEN];
    uint32_t ether_proto;
    char *data;
} sender_params_t;

static void parse_command_line_options(int argc, char **argv, void *params)
{
    int val;
    int option_index = 0;
    sender_params_t *sender_params = params;

    const struct option long_options[] = {
        {"interface", required_argument, NULL, 'I'},
        {"interval", required_argument, NULL, 'i'},
        {"count", required_argument, NULL, 'c'},
        {"packetsize", required_argument, NULL, 's'},
        {"dst_mac", required_argument, NULL, 'm'},
        {"ether_proto", required_argument, NULL, 'p'},
        {"data", required_argument, NULL, 'd'},
        {0, 0, 0, 0},
    };

    while (1) {
        val = getopt_long(argc, argv, "I:i:c:s:", long_options, &option_index);
        if (val == -1) {
            break;
        }

        switch (val) {
        case 0:
            printf("option %s", long_options[option_index].name);
            if (optarg)
                printf(" with arg %s", optarg);
            printf("\n");
            break;
        case 'I':
            strncpy(sender_params->interface, optarg, IFNAMSIZ - 1);
            printf("option interface with value '%s'\n", sender_params->interface);
            break;
        case 'i':
            sender_params->interval = strtoul(optarg, NULL, 0);
            printf("option interval with value '%d'\n", sender_params->interval);
            break;
        case 'c':
            sender_params->count = strtoul(optarg, NULL, 0);
            printf("option count with value '%d'\n", sender_params->count);
            break;
        case 's':
            sender_params->packetsize = strtoul(optarg, NULL, 0);
            printf("option packetsize with value '%d'\n", sender_params->packetsize);
            break;
        case 'm':
            sscanf(optarg, "%02x:%02x:%02x:%02x:%02x:%02x",
                   &sender_params->ether_mac[0],
                   &sender_params->ether_mac[1],
                   &sender_params->ether_mac[2],
                   &sender_params->ether_mac[3],
                   &sender_params->ether_mac[4],
                   &sender_params->ether_mac[5]);
            printf("option dst_mac with value '%s'\n", optarg);
            break;
        case 'p':
            sender_params->ether_proto = strtoul(optarg, NULL, 0);
            printf("option ether_proto with value '0x%04x'\n", sender_params->ether_proto);
            break;
        case 'd':
            sender_params->data = (void *)optarg;
            printf("option data with value '%s'\n", sender_params->data);
            break;
        }
    }
}

int main(int argc, char *argv[])
{
    int ret = 0;
    sender_params_t sender_params = {
        .interface = "eth0",
        .interval = 100,
        .packetsize = 0,
        .count = -1,
        .ether_proto = 0x8951,
        .data = "hello",
    };

    parse_command_line_options(argc, argv, &sender_params);

    char *sendbuf = malloc(sender_params.packetsize);
    if (sendbuf == NULL) {
        perror("malloc");
        return -1;
    }

    int sockfd;
    /* Open RAW socket to send on */
    if ((sockfd = socket(AF_PACKET, SOCK_RAW, IPPROTO_RAW)) == -1) {
        perror("socket");
        free(sendbuf);
        return -1;
    }

    struct ifreq if_idx = {};
    strncpy(if_idx.ifr_name, sender_params.interface, IFNAMSIZ - 1);
    /* Get the index of the interface to send on */
    if (ioctl(sockfd, SIOCGIFINDEX, &if_idx) < 0) {
        perror("SIOCGIFINDEX");
        ret = -1;
        goto end;
    }

    struct ifreq if_mac = {};
    strncpy(if_mac.ifr_name, sender_params.interface, IFNAMSIZ - 1);
    /* Get the MAC address of the interface to send on */
    if (ioctl(sockfd, SIOCGIFHWADDR, &if_mac) < 0) {
        perror("SIOCGIFHWADDR");
        ret = -1;
        goto end;
    }

    /* use MTU size if packetsize is 0 */
    if (sender_params.packetsize == 0) {
        struct ifreq if_mtu = {};
        strncpy(if_mtu.ifr_name, sender_params.interface, IFNAMSIZ - 1);
        if (ioctl(sockfd, SIOCGIFMTU, &if_mtu) < 0) {
            perror("SIOCGIFMTU");
            ret = -1;
            goto end;
        }
        sender_params.packetsize = if_mtu.ifr_mtu;
    }

    /* Construct the Ethernet header */
    int tx_len = 0;
    memset(sendbuf, 0, sender_params.packetsize);
    /* Ethernet header */
    struct ether_header *eh = (struct ether_header *)sendbuf;
    eh->ether_shost[0] = ((uint8_t *)&if_mac.ifr_hwaddr.sa_data)[0];
    eh->ether_shost[1] = ((uint8_t *)&if_mac.ifr_hwaddr.sa_data)[1];
    eh->ether_shost[2] = ((uint8_t *)&if_mac.ifr_hwaddr.sa_data)[2];
    eh->ether_shost[3] = ((uint8_t *)&if_mac.ifr_hwaddr.sa_data)[3];
    eh->ether_shost[4] = ((uint8_t *)&if_mac.ifr_hwaddr.sa_data)[4];
    eh->ether_shost[5] = ((uint8_t *)&if_mac.ifr_hwaddr.sa_data)[5];
    eh->ether_dhost[0] = (uint8_t)sender_params.ether_mac[0];
    eh->ether_dhost[1] = (uint8_t)sender_params.ether_mac[1];
    eh->ether_dhost[2] = (uint8_t)sender_params.ether_mac[2];
    eh->ether_dhost[3] = (uint8_t)sender_params.ether_mac[3];
    eh->ether_dhost[4] = (uint8_t)sender_params.ether_mac[4];
    eh->ether_dhost[5] = (uint8_t)sender_params.ether_mac[5];
    /* Ethertype field */
    eh->ether_type = htons(sender_params.ether_proto);
    tx_len += sizeof(struct ether_header);

    /* Packet data */
    int i, len = strlen(sender_params.data) + 1;
    for (i = 0; tx_len < sender_params.packetsize; tx_len++) {
        sendbuf[tx_len] = sender_params.data[i];
        i = (i + 1) % len;
    }
    sendbuf[sender_params.packetsize - 1] = 0;

    struct sockaddr_ll socket_address = {};
    /* Index of the network device */
    socket_address.sll_ifindex = if_idx.ifr_ifindex;
    /* Address length */
    socket_address.sll_halen = ETH_ALEN;
    /* Destination MAC */
    socket_address.sll_addr[0] = (uint8_t)sender_params.ether_mac[0];
    socket_address.sll_addr[1] = (uint8_t)sender_params.ether_mac[1];
    socket_address.sll_addr[2] = (uint8_t)sender_params.ether_mac[2];
    socket_address.sll_addr[3] = (uint8_t)sender_params.ether_mac[3];
    socket_address.sll_addr[4] = (uint8_t)sender_params.ether_mac[4];
    socket_address.sll_addr[5] = (uint8_t)sender_params.ether_mac[5];

    i = sender_params.count;
    while (i--) {
        /* Send packet */
        if (sendto(sockfd, sendbuf, tx_len, 0,
                   (struct sockaddr *)&socket_address,
                   sizeof(struct sockaddr_ll)) < 0) {
            perror("sendto");
            ret = -1;
            goto end;
        }
        /* Sleep for a while */
        usleep(sender_params.interval * 1000);
    }

end:
    close(sockfd);
    free(sendbuf);
    return ret;
}
