
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
#include <map>
#include <inttypes.h>


struct series_info {
	int32_t _min_sequence;
	int32_t _max_sequence;
};

struct client_statistics {
	int32_t _stat_delivery_time[LOAD_SERIES][REPORT_STATISTICS_FOR_PACKETS];
	series_info _stat_delivery_info;
	int32_t _stat_packets_count;

	client_statistics() {
		clear();
	}
	void clear() {
		_stat_packets_count = 0;
		NEGATIVE_RESET(_stat_delivery_time);
		NEGATIVE_RESET(_stat_delivery_info);
	}
};

typedef std::map<long, std::shared_ptr<client_statistics>> stat_map;
stat_map _statistics;

void reset_statistics(long cid)
{
	auto it = _statistics.find(cid);
	if(it!=_statistics.end())
		it->second->clear();
}

int update_statistics(load_payload* payload, long cid)
{
	std::pair<stat_map::iterator,bool> ret;
	ret = _statistics.insert(std::make_pair(cid, std::shared_ptr<client_statistics>(new client_statistics())));

	client_statistics& stat = *(ret.first->second.get());

	long now_time_stamp_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
			std::chrono::system_clock::now().time_since_epoch()).count();
	long delivery_time = now_time_stamp_ms - payload->_set_start_time_stamp;

	assert(payload->_load_set_count < LOAD_SERIES);
	assert(stat._stat_packets_count < REPORT_STATISTICS_FOR_PACKETS);
	stat._stat_delivery_time[payload->_load_set_count][stat._stat_packets_count] = (int) delivery_time;

	set_min(stat._stat_delivery_info._min_sequence, payload->_seqence_number);
	set_max(stat._stat_delivery_info._max_sequence, payload->_seqence_number);

	assert(stat._stat_delivery_info._min_sequence != -1);
	assert(stat._stat_delivery_info._max_sequence != -1);

	//printf("debug min %ld max %ld packet_sequenc %ld \n", stat_delivery_info._min_sequence, stat_delivery_info._max_sequence, payload->_seqence_number);
	assert(stat._stat_delivery_info._max_sequence - stat._stat_delivery_info._min_sequence < REPORT_STATISTICS_FOR_PACKETS);

	stat._stat_packets_count++;

	return stat._stat_packets_count;
}


void calculate_statistics(load_payload* payload, statistics_payload* st, long cid)
{
	stat_map::iterator it = _statistics.find(cid);
	if(it!=_statistics.end()) {
		client_statistics &stat = *(it->second.get());

		assert(payload->_load_set_count < LOAD_SERIES);
		int av_delivery = mediana(stat._stat_delivery_time[payload->_load_set_count]);
		printf(" delivery time %d\n", av_delivery);
		int32_t sent_packets = stat._stat_delivery_info._max_sequence - stat._stat_delivery_info._min_sequence + 1;
		float packet_lost = 100.0 - 100.0 * (float) REPORT_STATISTICS_FOR_PACKETS / (float) sent_packets;


		printf(" packet loss %f (sent:%" PRId32 " %" PRId32 "-%" PRId32 ", received: %d)\n",
			   packet_lost,
			   sent_packets,
			   stat._stat_delivery_info._min_sequence,
			   stat._stat_delivery_info._max_sequence,
			   REPORT_STATISTICS_FOR_PACKETS);

		st->_packets_count = REPORT_STATISTICS_FOR_PACKETS;
		st->_delivery_time = av_delivery;
		st->_packet_lost = static_cast<int>(packet_lost * 100.00);
	}
}

long client_id(sockaddr_in* sin) {
	long client_id = sin->sin_addr.s_addr;
	client_id = client_id << 16;
	client_id = client_id | sin->sin_port;
	return client_id;
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
		long cid = client_id((sockaddr_in*)&in_addr);

		//printf("received %d bytes (packet %d) from client %s\n", len, recv_packet->_type, inet_ntoa(((sockaddr_in*)&in_addr)->sin_addr));
		if(recv_packet->_type==ETimeSync)
			add_server_time_stamp(recv_packet);
		if(recv_packet->_type==ETimeSync || recv_packet->_type==EEcho) {
			reset_statistics(cid);
			ssize_t sent = ::sendto(sock, recv_buffer, len, 0, (struct sockaddr *) &in_addr, addr_len);
			if (sent != sizeof(packet)) {
				int err = errno;
				printf("could not send echo %s\n", strerror(errno));
			}
		}
		if(recv_packet->_type==ELoad) {

			load_payload* payload = reinterpret_cast<load_payload*>(&recv_packet->_payload[0]);

			if(update_statistics(payload, cid) == REPORT_STATISTICS_FOR_PACKETS) {

				out.clear();
				out._type = ELoadStatistics;

				printf("report generation \n");
				calculate_statistics(payload, reinterpret_cast<statistics_payload*>(&out._payload), cid);
				reset_statistics(cid);

				ssize_t sent = ::sendto(sock, &out, sizeof(out), 0, (struct sockaddr *)&in_addr, addr_len);
				if (sent != sizeof(packet)) {
					int err = errno;
					printf("could not send stats %s\n", strerror(errno));
				}

			}


		}


	}

	close(sock);

	return 0;
}
