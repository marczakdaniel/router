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

uint32_t create_netmask(uint8_t mask) {
    uint32_t netmask = 0;
    for (int i = 0; i < mask; i++) {
        netmask = netmask >> 1;
        netmask = netmask | 0x80000000;
    }
    return htonl(netmask);
}

struct in_addr broadcast_address(struct in_addr ip, uint32_t netmask) {
    struct in_addr result;
    result.s_addr = ip.s_addr | (~netmask);
    return result;
}

struct in_addr network_address(struct in_addr ip, uint32_t netmask) {
    struct in_addr result;
    result.s_addr = (uint32_t)ip.s_addr & netmask;
    return result;
}

// Code from THE GNU C LIBRARY: 
// https://www.gnu.org/software/libc/manual/html_node/Calculating-Elapsed-Time.html
int timeval_subtract (struct timeval *result, struct timeval *x, struct timeval *y) {
    /* Perform the carry for the later subtraction by updating y. */
    if (x->tv_usec < y->tv_usec) {
        int nsec = (y->tv_usec - x->tv_usec) / 1000000 + 1;
        y->tv_usec -= 1000000 * nsec;
        y->tv_sec += nsec;
    }
    if (x->tv_usec - y->tv_usec > 1000000) {
        int nsec = (x->tv_usec - y->tv_usec) / 1000000;
        y->tv_usec += 1000000 * nsec;
        y->tv_sec -= nsec;
    }

    /* Compute the time remaining to wait.
        tv_usec is certainly positive. */
    result->tv_sec = x->tv_sec - y->tv_sec;
    result->tv_usec = x->tv_usec - y->tv_usec;

    /* Return 1 if result is negative. */
    return x->tv_sec < y->tv_sec;
}

void read_configuration(char * argv, struct Route_info * Info) {
    FILE *fp;
    fp = fopen(argv, "r");
    fscanf(fp, "%d\n", &Info->n_interfaces);
    Info->Interfaces = (struct Interface *)malloc(Info->n_interfaces * sizeof(struct Interface));
    bzero(Info->Interfaces, Info->n_interfaces * sizeof(struct Interface));

    for (int i = 0; i < Info->n_interfaces; i++) {
        char ip_str[19];
        char dist_str[9];
        char * mask_str;
        fscanf(fp, "%s %s %d", 
            ip_str, dist_str, &Info->Interfaces[i].metric);
        mask_str = strchr(ip_str, '/');
        *mask_str = '\0';
        mask_str += 1;
        inet_pton(AF_INET, ip_str, &Info->Interfaces[i].ip);
        Info->Interfaces[i].mask = atoi(mask_str);
        Info->Interfaces[i].netmask = create_netmask(Info->Interfaces[i].mask);
        Info->Interfaces[i].broadcast_ip = 
            broadcast_address(Info->Interfaces[i].ip, Info->Interfaces[i].netmask);
        Info->Interfaces[i].network_ip =
            network_address(Info->Interfaces[i].ip, Info->Interfaces[i].netmask);
        Info->Interfaces[i].active = true;
        Info->Interfaces[i].stop_broadcast = false;
        //gettimeofday(&Interfaces[i].last_connect, NULL);
    }
    //printf("%d\n", Info->n_interfaces);
    fclose(fp);
}

void receive_packet(int sockfd, struct Route_info * Info) {
    fd_set descriptors;
    FD_ZERO(&descriptors);
    FD_SET(sockfd, &descriptors);
    struct timeval tv; tv.tv_sec = RECEIVE_TIME; tv.tv_usec = 0;
    struct timeval begin, end;
    int ready = 1;
    gettimeofday(&begin, NULL);
    for (;;) {
        ready = select(sockfd+1, &descriptors, NULL, NULL, &tv);
        if (ready < 0) {
            fprintf(stderr, "select error: %s\n", strerror(errno));
            exit(EXIT_FAILURE);
        }
        if (ready == 0) {
            break;
        }
        for (;;) {
            struct sockaddr_in sender;
            socklen_t sender_len = sizeof(sender);
            uint8_t buffer[IP_MAXPACKET+1];
            ssize_t datagram_len = 
                recvfrom (sockfd, buffer, IP_MAXPACKET, 
                    MSG_DONTWAIT, (struct sockaddr*)&sender, &sender_len);
            if (datagram_len == -1) {
                if (errno == EWOULDBLOCK) { // normal error
                    break;
                }
                else {
                    fprintf(stderr, "recvfrom error: %s\n", strerror(errno));
                    exit(EXIT_FAILURE);
                }
            }
            else if (datagram_len == 0) {
                fprintf(stderr, "recvfrom: connection closed\n");
                exit(EXIT_FAILURE);
            }
            analyse_datagram(buffer, &sender, Info);
        }  
        gettimeofday(&end, 0);
        struct timeval result;
        timeval_subtract(&result, &end, &begin);
        struct timeval tv_new;
        if (timeval_subtract(&tv_new, &tv, &result) == 1) {
            break;
        }
        tv = tv_new;
    }
}

bool is_directly(struct in_addr ip, struct Route_info * Info) {
    struct Interface * Interfaces = Info->Interfaces;
    int n_inter = Info->n_interfaces;
    for (int i = 0; i < n_inter; i++) {
        if (network_address(ip, Interfaces[i].mask).s_addr == Interfaces[i].network_ip.s_addr) {
            return true;
        }
    }
    return false;
}

bool is_my_interface(struct in_addr ip, struct Route_info * Info) {
    struct Interface * Interfaces = Info->Interfaces;
    int n_inter = Info->n_interfaces;
    for (int i = 0; i < n_inter; i++) {
        if (ip.s_addr == Interfaces[i].ip.s_addr) {
            return true;
        }
    }
    return false;
}

bool is_my_network(struct in_addr ip, struct Route_info * Info) {
    struct Interface * Interfaces = Info->Interfaces;
    int n_inter = Info->n_interfaces;
    for (int i = 0; i < n_inter; i++) {
        if (ip.s_addr == Interfaces[i].network_ip.s_addr) {
            return true;
        }
    }
    return false;
}

int interface_number(struct in_addr ip, struct Route_info * Info) {
    struct Interface * Interfaces = Info->Interfaces;
    int n_inter = Info->n_interfaces;
    for (int i = 0; i < n_inter; i++) {
        if (network_address(ip, Interfaces[i].netmask).s_addr == Interfaces[i].network_ip.s_addr) {
            return i;
        }
    }
    return -1;
}

void analyse_datagram(uint8_t * datagram, 
            struct sockaddr_in * sender, 
            struct Route_info * Info) {

    std::map<uint32_t, struct Route_entry> * Route_table = Info->Route_table;
    struct Interface * Interfaces = Info->Interfaces;
    int n_inter = Info->n_interfaces;

    /* Is it valid packet? - packet that my programs send */

    /* Is it send from my interfaces? - receive packet that I send */
    if (is_my_interface(sender->sin_addr, Info)) {
        return;
    }

    struct Route_entry entry;
    bzero(&entry, sizeof(entry));
    entry.network_ip.s_addr = *((uint32_t *)datagram);
    entry.mask = *((uint8_t *)(datagram + 4));
    entry.metric = ntohl(*((uint32_t *)(datagram + 5)));
    entry.netmask = create_netmask(entry.mask);
    entry.gateway_ip = sender->sin_addr;
    entry.stop_broadcast = false;
    gettimeofday(&entry.last_contact, 0);

    int inter_number = interface_number(entry.gateway_ip, Info);
    if (inter_number == -1) {
        char ip_str[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &entry.gateway_ip, ip_str, INET_ADDRSTRLEN);
        fprintf(stderr, "analyse detagram error: interface number: %s\n", ip_str);
        exit(EXIT_FAILURE);
    }

    /* Is it packet about my network from gateway that is in the same network as me? */
    if (entry.network_ip.s_addr == network_address(sender->sin_addr, Interfaces[inter_number].metric).s_addr) {
        return;
    }

    entry.directly = is_directly(entry.network_ip, Info);

    if (!entry.directly) {
        entry.metric += Interfaces[inter_number].metric;

        uint32_t r_key = (uint32_t)entry.network_ip.s_addr;
        std::map<uint32_t, struct Route_entry>::iterator itr;
        itr = (*Route_table).find(r_key);
        if (itr == (*Route_table).end()) {
            (*Route_table).insert({r_key, entry});
        }
        else {
            if (itr->second.gateway_ip.s_addr == entry.gateway_ip.s_addr) {
                itr->second.metric = entry.metric;
                itr->second.last_contact = entry.last_contact;
            }
            else {
                uint32_t old_metric = itr->second.metric;
                uint32_t new_metric = entry.metric;
                if (new_metric < old_metric) {
                    itr->second = entry;
                }
            }
        }
    }
}

void send_route_table(int sockfd, struct Route_info * Info) {
    struct Interface * Interfaces = Info->Interfaces;
    std::map<uint32_t, struct Route_entry> * Route_table = Info->Route_table;
    int n_inter =  Info->n_interfaces;

    for (int i = 0; i < n_inter; i++) {
        struct sockaddr_in s;
        bzero(&s, sizeof(s));
        s.sin_family = AF_INET;
        s.sin_port = htons(54321);
        s.sin_addr = Interfaces[i].broadcast_ip;
        //char ip_str[INET_ADDRSTRLEN];
        //inet_ntop(AF_INET, &Interfaces[i].broadcast_ip, ip_str, INET_ADDRSTRLEN);
        //printf("%s\n", ip_str);

        /* Send routing table */
        std::map<uint32_t, struct Route_entry>::iterator itr;
        for (itr = (*Route_table).begin(); itr != (*Route_table).end(); itr++) {
            struct Route_entry entry = itr->second;
            if (!(entry.directly && entry.stop_broadcast == true)) {
                send_entry(sockfd, &entry, s, &Interfaces[i]);
            }
        }
    }
}

int send_entry(int sockfd_send, struct Route_entry * entry, 
        struct sockaddr_in server_address, struct Interface * inter) {

    uint8_t buffer[9];
    *((uint32_t *)buffer) = (uint32_t)entry->network_ip.s_addr;
    *((uint8_t *)(buffer + 4)) = (uint8_t)entry->mask;
    *((uint32_t *)(buffer + 5)) = htonl((uint32_t)entry->metric);
    ssize_t buffer_len = sizeof(buffer);

    if (sendto(sockfd_send, buffer, buffer_len, 0, 
            (struct sockaddr*)&server_address, 
            sizeof(server_address)) < 0) {
        inter->active = false;
		//fprintf(stderr, "sendto error: %s\n", strerror(errno)); 
		return EXIT_FAILURE;		
	}
    inter->active = true;
    return EXIT_SUCCESS;
}

void handling_unreachable(struct Route_info * Info) {
    struct Interface * Interfaces = Info->Interfaces;
    std::map<uint32_t, struct Route_entry> * Route_table = Info->Route_table;
    int n_inter = Info->n_interfaces;

    /* Unactive interface - sento return error*/
    for (int i = 0; i < n_inter; i++) {
        if (Interfaces[i].active == false) {
            std::map<uint32_t, struct Route_entry>::iterator itr;
            for (itr = (*Route_table).begin(); itr != (*Route_table).end(); itr++) {
                if (itr->second.directly) {
                    if (itr->second.network_ip.s_addr == Interfaces[i].network_ip.s_addr) {
                        itr->second.metric = INFINITY;
                    }
                }
                else {
                    if (network_address(itr->second.gateway_ip, Interfaces[i].mask).s_addr == Interfaces[i].network_ip.s_addr) {
                        itr->second.metric = INFINITY;
                    }
                }
            }
        }
    }
    
    /* Neighbour unactive */
    std::map<uint32_t, struct Route_entry>::iterator itr;
    for (itr = (*Route_table).begin(); itr != (*Route_table).end(); itr++) {
        if (!itr->second.directly) {
            struct timeval time_now;

            struct timeval time_last_connect;
            time_last_connect.tv_sec = itr->second.last_contact.tv_sec;
            time_last_connect.tv_usec = itr->second.last_contact.tv_usec;

            struct timeval result;

            struct timeval unreachable_time;
            unreachable_time.tv_sec = UNREACHABLE_TIME;
            unreachable_time.tv_usec = 0;

            struct timeval tmp;

            gettimeofday(&time_now, NULL);

            timeval_subtract(&result, &time_now, &time_last_connect);

            if (timeval_subtract(&tmp, &unreachable_time, &result) == 1) {
                itr->second.metric = INFINITY;
            } 
        }
    }

    /* Delate or stop broadcast */
    itr = (*Route_table).begin();
    while (itr != (*Route_table).end()) {
        if (itr->second.metric >= INFINITY) {
            struct timeval time_now;

            struct timeval time_last_connect;
            time_last_connect.tv_sec = itr->second.last_not_infinity.tv_sec;
            time_last_connect.tv_usec = itr->second.last_not_infinity.tv_usec;

            struct timeval result;

            struct timeval delate_time;
            delate_time.tv_sec = DELATE_TIME;
            delate_time.tv_usec = 0;

            struct timeval tmp;

            gettimeofday(&time_now, NULL);

            timeval_subtract(&result, &time_now, &time_last_connect);

            if (timeval_subtract(&tmp, &delate_time, &result) == 1) {
                if (itr->second.directly) {
                    itr->second.stop_broadcast = true;
                }
                else {
                    itr = (*Route_table).erase(itr);
                    continue;
                }
            }
        }
        else {
            gettimeofday(&itr->second.last_not_infinity, NULL);
        }
        itr++;
    } 
}

struct Route_entry interface_to_route(struct Interface * inter) {
    struct Route_entry entry;
    bzero(&entry, sizeof(struct Route_entry));
    entry.network_ip = inter->network_ip;
    entry.directly = true;
    entry.metric = inter->metric;
    entry.mask = inter->mask;
    entry.netmask = inter->netmask;
    entry.stop_broadcast = false;
    return entry;
}

void interface_care(struct Route_info * Info) {
    struct Interface * Interfaces = Info->Interfaces;
    std::map<uint32_t, struct Route_entry> * Route_table = Info->Route_table;
    int n_inter = Info->n_interfaces;

    for (int i = 0; i < n_inter; i++) {
        if (Interfaces[i].active) {
            struct Route_entry entry = interface_to_route(&Interfaces[i]);

            uint32_t r_key = (uint32_t)Interfaces[i].network_ip.s_addr;
            std::map<uint32_t, struct Route_entry>::iterator itr;
            itr = (*Route_table).find(r_key);
            if (itr == (*Route_table).end()) {
                (*Route_table).insert({r_key, entry});
            }
            else {
                (*Route_table)[r_key] = entry;
            }
        }
        else {
            struct Route_entry entry = interface_to_route(&Interfaces[i]);

            uint32_t r_key = (uint32_t)Interfaces[i].network_ip.s_addr;
            std::map<uint32_t, struct Route_entry>::iterator itr;
            itr = (*Route_table).find(r_key);
            if (itr == (*Route_table).end()) {
                entry.metric = INFINITY;
                (*Route_table).insert({r_key, entry});
            }
        }
    }
}

void print_routing_table(struct Route_info * Info) {
    struct Interface * Interfaces = Info->Interfaces;
    std::map<uint32_t, struct Route_entry> * Route_table = Info->Route_table;
    int n_inter = Info->n_interfaces;

    std::map<uint32_t, struct Route_entry>::iterator itr;

    /* Directly conntected */
    for (itr = (*Route_table).begin(); itr != (*Route_table).end(); itr++) {
        struct Route_entry entry = itr->second;
        if (entry.directly == true) {
            char ip_str[INET_ADDRSTRLEN];
            inet_ntop(AF_INET, &entry.network_ip, ip_str, INET_ADDRSTRLEN);
            printf("%s/%d ", ip_str, entry.mask);
            if (entry.metric < INFINITY) {
                printf("distance %u ", entry.metric);
            }
            else {
                printf("unreachable ");
            }
            printf("connected directly\n");
        }
    }
    for (itr = (*Route_table).begin(); itr != (*Route_table).end(); itr++) {
        struct Route_entry entry = itr->second;
        if (entry.directly == false) {
            char ip_str[INET_ADDRSTRLEN];
            char gate_str[INET_ADDRSTRLEN];
            inet_ntop(AF_INET, &entry.network_ip, ip_str, INET_ADDRSTRLEN);
            inet_ntop(AF_INET, &entry.gateway_ip, gate_str, INET_ADDRSTRLEN);
            printf("%s/%d distance %u via %s\n", ip_str, entry.mask, entry.metric, gate_str);
        }
    }
    printf("\n");
}