
#include <stdio.h>
#include <string.h>

#include <arpa/inet.h>
#include <netinet/in.h>
#include <errno.h>
#include "packet.h"

#include <iostream>
#include <chrono>
#include <ctime>
#include <thread>
#include <unistd.h>
#include <assert.h>


struct series_info {
	long _min_sequence;
	long _max_sequence;
};
int stat_delivery_time[LOAD_SERIES][REPORT_STATISTICS_FOR_PACKETS];
series_info stat_delivery_info;
int stat_packets_count = 0;

void reset_statistics()
{
	// reset to initial
	stat_packets_count = 0;
	NEGATIVE_RESET(stat_delivery_time);
	NEGATIVE_RESET(stat_delivery_info);
}

int main(int argc, char *argv[]) {

	struct sockaddr_in server_address;
	memset(&server_address, 0, sizeof(server_address));
	server_address.sin_family = AF_INET;
	server_address.sin_port = htons(SERVER_PORT);
	server_address.sin_addr.s_addr = htonl(INADDR_ANY);

	int sock;
	if ((sock = socket(PF_INET, SOCK_DGRAM, 0)) < 0) {
		printf("could not create socket\n");
		return 1;
	}

	if ((bind(sock, (struct sockaddr *)&server_address, sizeof(server_address))) < 0) {
		printf("could not bind socket\n");
		return 1;
	}

	sockaddr_storage in_addr;
	socklen_t addr_len = sizeof(sockaddr_in); // addr_len = sizeof(sockaddr_in6);
	char recv_buffer[2048];

	reset_statistics();

	packet out;

	for(;;) {

		int len = ::recvfrom(sock, recv_buffer, sizeof(recv_buffer), 0, (sockaddr *) &in_addr, &addr_len);
		if (len==-1) {
			int err = errno;
			printf("could not read from socket %s\n", strerror(errno));
			if(err == EAGAIN)
				continue;
			return 1;
		} else if (len == sizeof(recv_buffer))
			{
				printf("read buffer overflow\n");
				continue;
			}
		packet* recv_packet = reinterpret_cast<packet*> (&recv_buffer[0]);
		//printf("received %d bytes (packet %d) from client %s\n", len, recv_packet->_type, inet_ntoa(((sockaddr_in*)&in_addr)->sin_addr));
		if(recv_packet->_type==ETimeSync)
			add_server_time_stamp(recv_packet);
		if(recv_packet->_type==ETimeSync || recv_packet->_type==EEcho) {
			reset_statistics();
			ssize_t sent = ::sendto(sock, recv_buffer, len, 0, (struct sockaddr *) &in_addr, addr_len);
			if (sent != sizeof(packet)) {
				int err = errno;
				printf("could not send echo %s\n", strerror(errno));
			}
		}
		if(recv_packet->_type==ELoad) {

			load_payload* payload = reinterpret_cast<load_payload*>(&recv_packet->_payload[0]);

			long time_stamp_ms = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
			long delivery_time = time_stamp_ms - payload->_set_start_time_stamp;

			assert(payload->_load_set_count<LOAD_SERIES);
			assert(stat_packets_count<REPORT_STATISTICS_FOR_PACKETS);
			stat_delivery_time[payload->_load_set_count][stat_packets_count] = (int)delivery_time;

			set_min(stat_delivery_info._min_sequence, payload->_seqence_number);
			set_max(stat_delivery_info._max_sequence, payload->_seqence_number);

			assert(stat_delivery_info._min_sequence!=-1);
			assert(stat_delivery_info._max_sequence!=-1);

			//printf("debug min %ld max %ld packet_sequenc %ld \n", stat_delivery_info._min_sequence, stat_delivery_info._max_sequence, payload->_seqence_number);
			assert(stat_delivery_info._max_sequence - stat_delivery_info._min_sequence < REPORT_STATISTICS_FOR_PACKETS);

			stat_packets_count++;
			if(stat_packets_count == REPORT_STATISTICS_FOR_PACKETS) {

				printf("report generation \n");

				assert(payload->_load_set_count < LOAD_SERIES);
				int av_delivery = mediana(stat_delivery_time[payload->_load_set_count]);
				printf(" delivery time %d\n", av_delivery);
				long sent_packets = stat_delivery_info._max_sequence - stat_delivery_info._min_sequence + 1;
				float packet_lost = 100.0 - 100.0*(float)REPORT_STATISTICS_FOR_PACKETS/(float)sent_packets;
				printf(" packet loss %f (sent:%ld %ld-%ld, received: %d)\n",
					    packet_lost,
						sent_packets,
						stat_delivery_info._min_sequence,
						stat_delivery_info._max_sequence,
						REPORT_STATISTICS_FOR_PACKETS);

				reset_statistics();
				assert(stat_delivery_info._min_sequence == -1);
				assert(stat_delivery_info._max_sequence == -1);

				// send the stat packet
				out.clear();
				assert(stat_delivery_info._min_sequence!=0);
				assert(stat_delivery_info._max_sequence!=0);
				out._type = ELoadStatistics;
				statistics_payload* st = reinterpret_cast<statistics_payload*>(&out._payload);
				st->_packets_count = REPORT_STATISTICS_FOR_PACKETS;
				st->_delivery_time = av_delivery;
				st->_packet_lost = static_cast<int>(packet_lost * 100.00);
				ssize_t sent = ::sendto(sock, &out, sizeof(out), 0, (struct sockaddr *)&in_addr, addr_len);
				if (sent != sizeof(packet)) {
					int err = errno;
					printf("could not send stats %s\n", strerror(errno));
				}

				assert(stat_delivery_info._min_sequence!=0);
				assert(stat_delivery_info._max_sequence!=0);

			}


		}


	}

	close(sock);

	return 0;
}
