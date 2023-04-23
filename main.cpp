// Daniel Marczak - 324351

#include "router.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <errno.h>
#include <map>
#include <netinet/ip.h>

int main(int argc, char* argv[]) {
    if (argc != 2) {
        fprintf(stderr, "invalid input\n");
        return EXIT_FAILURE;
    }

    /* Prepare structures */
    struct Route_info Info;   
    std::map<uint32_t, struct Route_entry> Route_table;
    Info.Route_table = &Route_table;

    /* Read configuration file */
    read_configuration(argv[1], &Info);

    /* Create socket to receive */

    int sockfd_receive = socket(AF_INET, SOCK_DGRAM, 0);
    if (sockfd_receive < 0) {
        fprintf(stderr, "socket error: %s\n", strerror(errno)); 
	    return EXIT_FAILURE;
    }

    struct sockaddr_in server_address;
    bzero(&server_address, sizeof(server_address));
    server_address.sin_family = AF_INET;
    server_address.sin_port = htons(54321);
    server_address.sin_addr.s_addr = htonl(INADDR_ANY);
    if (bind (sockfd_receive, (struct sockaddr*)&server_address, sizeof(server_address)) < 0) {
		fprintf(stderr, "bind error: %s\n", strerror(errno)); 
		return EXIT_FAILURE;
	}

    /* Create socket to send */

    int sockfd_send = socket(AF_INET, SOCK_DGRAM, 0);
    if (sockfd_send < 0) {
        fprintf(stderr, "socket error: %s\n", strerror(errno)); 
	    return EXIT_FAILURE;
    }

    int broadcastPermission = 1;
    setsockopt(sockfd_send, SOL_SOCKET, SO_BROADCAST,
        (void *)&broadcastPermission, sizeof(broadcastPermission));

    /* Main loop */
    for (;;) {
        /* Receiving and analysing packets */
        receive_packet(sockfd_receive, &Info);

        /* Sending packets */
        send_route_table(sockfd_send, &Info);

        /* Handling unreachable network */
        handling_unreachable(&Info);

        /* Interface care */
        interface_care(&Info);

        /* Print routing table*/
        print_routing_table(&Info);
    }

    /* Cleaning */
    free(Info.Interfaces);
    close(sockfd_receive);
    close(sockfd_send);
}