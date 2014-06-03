#include <pthread.h>
#include <string.h>
#include <stdio.h>

#include <sys/types.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <net/if.h>
#include <net/ethernet.h>
#include <net/if_arp.h>
#include <netinet/in.h>

#include <mysql/mysql.h>

#define INTERFAZ "wlan0"
#define RESPUESTA "Respuesta: La IP %s pertenece a %02X:%02X:%02X:%02X:%02X:%02X\n"
#define MAC_TEMPLATE "%02X:%02X:%02X:%02X:%02X:%02X"
#define IP_TEMPLATE "%u.%u.%u.%u"

struct msgARP{
	unsigned char destinoEthernet[6];
	unsigned char origenEthernet[6];
	unsigned short tipoEthernet;
	unsigned short tipoHardware;
	unsigned short protocolo;
	unsigned char longitudMAC;
	unsigned char longitudRed;
	unsigned short tipoARP;
	unsigned char origenMAC[6];
	unsigned char origenIP[4];
	unsigned char destinoMAC[6];
	unsigned char destinoIP[4];
};

pthread_t tid[2];

void *sendRequests()
{
	int arp_socket, ifreq_socket, optval, n;
	struct msgARP msg;
	struct ifreq ifr;
	struct sockaddr sa;

	//Get MAC address
	if((ifreq_socket = socket(AF_INET, SOCK_PACKET, htons(ETH_P_ARP))) < 0)
		{
			perror("ERROR al abrir socket");
			return -1;
		}

	strcpy(ifr.ifr_name, INTERFAZ);
	if(ioctl(ifreq_socket, SIOCGIFHWADDR, &ifr) < 0)
		{
			perror("ERROR al obtener MAC origen");
			return -1;
		}

	memcpy(&msg.origenEthernet, &ifr.ifr_hwaddr.sa_data, 6);
	memcpy(&msg.origenMAC, &ifr.ifr_hwaddr.sa_data, 6);
	
	if(ioctl(ifreq_socket, SIOCGIFADDR, &ifr) < 0)
		{
			perror("ERROR al obtener IP origen");
			return -1;
		}

	memcpy(&msg.origenIP, &ifr.ifr_hwaddr.sa_data[2], 4);

	close(ifreq_socket);

	//Fill most of the packet
	memset(&msg.destinoEthernet, 0xff, 6);
	msg.tipoEthernet = htons(ETH_P_ARP);
	msg.tipoHardware = htons(ARPHRD_ETHER);
	msg.protocolo = htons(ETH_P_IP);
	msg.longitudMAC = 6;
	msg.longitudRed = 4;
	msg.tipoARP = htons(ARPOP_REQUEST);
	memset(&msg.destinoMAC, 0x00, 6);

	//Establish subnet
	memcpy(&msg.destinoIP, &msg.origenIP, 3);

	//Send 254 ARP requests
	int i = 1;

	while(i < 255)
	{
		msg.destinoIP[3] = i;

		if((arp_socket = socket(AF_INET, SOCK_PACKET, htons(ETH_P_ARP))) < 0)
		{
			perror("ERROR al abrir socket");
			return -1;
		}

		if(setsockopt(arp_socket, SOL_SOCKET, SO_BROADCAST, &optval, sizeof(optval)) < 0) 
		{
			perror("ERROR en la funcion setsockopt");
			return -1;
		}

		memset(&sa, 0x00, sizeof(sa));
		strcpy(sa.sa_data, INTERFAZ);

		if(sendto(arp_socket, &msg, sizeof(msg), 0, (struct sockaddr *) &sa, sizeof(sa)) < 0)
		{
			perror("ERROR al enviar");
			return -1;
		}

		i++;

		close(arp_socket);

	}

}

void * receiveReplies()
{
	int s, n, i;
	struct msgARP msg;
	struct sockaddr sa;
	unsigned char unaMac[6];
	unsigned char unaIp[4];

	if((s = socket(AF_INET, SOCK_PACKET, htons(ETH_P_ARP))) < 0)
		{
			perror("ERROR al abrir socket");
			return -1;
		}


	i = 0;
	do{
		memset(&sa, 0x00, sizeof(sa));
		memset(&msg, 0x00,  sizeof(msg));
		n = sizeof(sa);
		if(recvfrom(s, &msg, sizeof(msg), 0, (struct sockaddr *) &sa, &n) < 0)
			{
				perror("ERROR al recibir");
				return -1;
			}
		if((ntohs(msg.tipoARP) == ARPOP_REPLY))
		{
			i++;

			memcpy(unaMac, &msg.origenMAC, 6);
			memcpy(unaIp, &msg.origenIP, 4);

			guardarEnTabla(unaMac, unaIp);

		}
		
		}while(1);

}

void guardarEnTabla(unsigned char * mac, unsigned char * ip)
{
	char ip_str[20];
	char mac_str[20];

	MYSQL *conn;
	
	char *server = "localhost";
	char *user = "root";
	char *password = "//lsoazules"; 
	char *database = "redes";

	//Convertir mac e ip a cadenas
	sprintf(mac_str, MAC_TEMPLATE, 
						mac[0], mac[1], mac[2], mac[3],
						mac[4], mac[5]
						); 

	sprintf(ip_str, IP_TEMPLATE, ip[0], ip[1], ip[2], ip[3]);

	conn = mysql_init(NULL);
	if(!mysql_real_connect(conn, server, user, password, database, 0, NULL, 0))	
	{	
		fprintf(stderr, "ERROR para conectarse a mysql", mysql_error(conn));
		return -1;
	}
	else
	{
		char query[80] = "insert into tablaARP (mac, ip) values (\'";
			strcat(query, mac_str);
			strcat(query, "\',\'");
			strcat(query, ip_str);
			strcat(query, "\')");

			printf("\nQUERY: %s\n", query);

		if(mysql_query(conn, query))
			fprintf(stderr, "\nNEL con el query\n", mysql_error(conn));		
	}

	mysql_close(conn);
}

int main()
{
	int err;
	err = pthread_create(&(tid[0]), NULL, &sendRequests, NULL);
        if (err != 0)
            printf("\ncan't create thread :[%s]", strerror(err));
        else
            printf("\n Sending...\n");

     err = pthread_create(&(tid[1]), NULL, &receiveReplies, NULL);
        if (err != 0)
            printf("\ncan't create thread :[%s]", strerror(err));
        else
            printf("\n Receiving...\n");


    sleep(30);
    printf("\nBYE\n"); 
}