// Daniel Marczak - 324351

#ifndef ROUTER_H
#define ROUTER_H

#include <netinet/in.h>
#include <stdbool.h>
#include <sys/time.h>
#include <map>

#define INFINITY 16
#define RECEIVE_TIME 10
#define UNREACHABLE_TIME 20
#define DELATE_TIME 20

struct Route_info {
    int n_interfaces;
    struct Interface * Interfaces;
    std::map<uint32_t, struct Route_entry> * Route_table;
};

struct Interface {
    struct in_addr ip;
    struct in_addr broadcast_ip;
    struct in_addr network_ip;
    uint8_t mask;
    uint32_t netmask;
    uint32_t metric;
    bool active;
    bool stop_broadcast;
};

struct Route_entry {
    struct in_addr network_ip; // network
    uint8_t mask; // network
    uint32_t netmask; // network
    struct in_addr gateway_ip; //network
    uint32_t metric; // host
    bool directly;
    bool stop_broadcast;
    struct timeval last_contact;
    struct timeval last_not_infinity;
};

uint32_t create_netmask(uint8_t mask);
struct in_addr broadcast_address(struct in_addr ip, uint32_t netmask);
struct in_addr network_address(struct in_addr ip, uint32_t netmask);
int timeval_subtract (struct timeval *result, struct timeval *x, 
                        struct timeval *y);
void read_configuration(char * argv, struct Route_info * Info);
void receive_packet(int sockfd, struct Route_info * Info);
void send_route_table(int sockfd, struct Route_info * Info);
void handling_unreachable(struct Route_info * Info);
void print_routing_table(struct Route_info * Info);
void interface_care(struct Route_info * Info);
void analyse_datagram(uint8_t * datagram, struct sockaddr_in * sender, struct Route_info * Info);
int send_entry(int sockfd_send, struct Route_entry * entry, struct sockaddr_in server_address, struct Interface * inter);
#endif // ROUTER_H