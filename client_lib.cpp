//
// Created by Oleg Golosovskiy on 08/10/2018.
//

#include "client_lib.h"

to_print_callback* g_logger = 0;

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wformat-security"
template<typename ... Args>
static void print_log(char const* fmt, Args ... args)
{
	if(!g_logger)
		return;

	size_t size = snprintf(nullptr, 0, fmt, args ...) + 1;
	std::unique_ptr<char[]> buf(new char[size]);

	snprintf(buf.get(), size, fmt, args ...);
	std::string formatted(buf.get(), buf.get() + size - 1);

	(*g_logger)(formatted.c_str());
}
#pragma clang diagnostic pop

client_lib::client_lib(to_print_callback* log)
{
	g_logger = log;
}

int client_lib::run_test(char const* server) {

	struct sockaddr_in server_address;
	memset(&server_address, 0, sizeof(server_address));
	server_address.sin_family = AF_INET;
	server_address.sin_port = htons(SERVER_PORT);
	//server_address.sin_addr.s_addr = htonl(INADDR_ANY);
	server_address.sin_addr.s_addr = inet_addr (server);


	int sock;
	if ((sock = socket(PF_INET, SOCK_DGRAM, 0)) < 0) {
		print_log("could not create socket\n");
		return -1;
	}

	struct timeval tv;
	tv.tv_sec = 0;
	tv.tv_usec = 500000;
	if (setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO,&tv,sizeof(tv)) < 0) {
		print_log("Error setsockopt");
		return -1;
	}

	int av_desync = 0;
	int av_rtt = 0;
	int av_delivery = 0;
	int av_packet_lost = 0;

	// measure RTT without load and clock desync
	{
		print_log("\nmeasure RTT (without load) and clock desync\n");
		packet out;
		out._type = ETimeSync;
		int rtt[RTT_MAX_ATTEMPTS];
		int clock_desync[RTT_MAX_ATTEMPTS];
		NEGATIVE_RESET(rtt);
		NEGATIVE_RESET(clock_desync);
		int position = 0;

		for (int attempt = 0; attempt < RTT_MAX_ATTEMPTS; ++attempt) {

			print_log(".");
			std::chrono::time_point<std::chrono::system_clock> now;
			now = std::chrono::system_clock::now();
			time_sync_payload* payload = reinterpret_cast<time_sync_payload*>(&out._payload[0]);
			payload->_client_time_stamp = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count();
			::sendto(sock, &out, sizeof(packet), 0, (struct sockaddr *) &server_address, sizeof(sockaddr_in));

			char recv_buffer[2048];
			sockaddr_storage in_addr;
			socklen_t in_addr_len = sizeof(sockaddr_in);

			int len = ::recvfrom(sock, recv_buffer, sizeof(recv_buffer), 0, (sockaddr *) &in_addr, &in_addr_len);
			if (len == -1) {
				int err = errno;
				if (err == ETIMEDOUT || err == EAGAIN) {
					now = std::chrono::system_clock::now();
					long err_time = std::chrono::duration_cast<std::chrono::milliseconds>(
							now.time_since_epoch()).count();
					print_log("\ntime out %s %d\n", strerror(err), (int) (err_time - payload->_client_time_stamp));
					continue;
				}
				print_log("\ncould not read from socket %s\n", strerror(err));
				return 1;
			} else if (len == sizeof(recv_buffer)) {
				print_log("read buffer overflow\n");
				continue;
			}
			// printf("received: %d byte from server %s\n", len, inet_ntoa(((sockaddr_in *) &in_addr)->sin_addr));
			now = std::chrono::system_clock::now();
			long milisecs2 = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count();
			packet *recv_packet = reinterpret_cast<packet *> (&recv_buffer[0]);
			long *recv_milisec = reinterpret_cast<long *> (recv_packet->_payload);
			long *recv_server_milisec = reinterpret_cast<long *> (recv_packet->_payload + sizeof(long));
			rtt[position] = milisecs2 - *recv_milisec;
			long server_time = *recv_server_milisec - (rtt[position] / 2);
			clock_desync[position] = *recv_milisec - server_time;
			// printf("rtt: %d clock desync: %d\n", rtt[position], clock_desync[position]);
			++position;
			std::this_thread::sleep_for(std::chrono::milliseconds(250));
		}

		av_desync = mediana(clock_desync);
		av_rtt = mediana(rtt);
		print_log("\nAverage RTT: %d ms\nAverage clock out of synchronization: %d ms\n", av_rtt, av_desync );
	}


	// make socket non blocking
	bool blocking = true;
	int flags = fcntl(sock, F_GETFL, 0);
	if (flags == -1)
		return 0;
	flags = blocking ? (flags & ~O_NONBLOCK) : (flags | O_NONBLOCK);
	if (fcntl(sock, F_SETFL, flags) == -1)
		return 0;

	long global_seqence_count = 0;
	int received_reports = 0;

	// 1.5 Upload MBits test
	{
		print_log("\nmeasure 1.5 Upload MBits test\n");
		packet out;
		int delivery_time[MAX_REPORTS];
		NEGATIVE_RESET(delivery_time);
		int packet_lost[MAX_REPORTS];
		NEGATIVE_RESET(packet_lost);

		for(int attempt = 0; attempt<LOAD_SERIES; ++attempt) {

			// send Media Packet
			// how much packets we will send for this set
			int packets = load_bits / 8 /*bits->bytes*/ /  /*bytes->packets*/ sizeof(packet) / /*4 times per second*/ packets_per_seconds;
			out.clear();
			out._type = ELoad;
			load_payload* payload = reinterpret_cast<load_payload*>(&out._payload[0]);
			payload->_load_set_count = attempt;
			payload->_load_set_packets = packets;

			long time_stamp_ms = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
			time_stamp_ms += av_desync;
			payload->_set_start_time_stamp = time_stamp_ms;
			//printf("send %d packest by %lu size\n", packets, sizeof(packet));
			print_log(".");
			for(int num_packet = 0; num_packet<packets; ++num_packet) {
				payload->_seqence_number = global_seqence_count++;
				::sendto(sock, &out, sizeof(packet), 0, (struct sockaddr *) &server_address, sizeof(sockaddr_in));
			}

			std::this_thread::sleep_for(std::chrono::milliseconds(1000/packets_per_seconds));

			// check stats packet
			char recv_buffer[load_statistic_buffer];
			sockaddr_storage in_addr;
			socklen_t in_addr_len = sizeof(sockaddr_in);

			while(true)
			{
				int len = ::recvfrom(sock, recv_buffer, sizeof(recv_buffer), 0, (sockaddr *) &in_addr, &in_addr_len);
				if (len == -1) {
					int err = errno;
					if (err != ETIMEDOUT && err != EAGAIN) {
						print_log("could not read stat packet %s\n", strerror(err));
						return 1;
					}
					break;
				} else if (len == sizeof(recv_buffer)) {
					print_log("read buffer overflow\n");
					return 1;
				} else {
					//printf("received: %d byte from server %s\n", len, inet_ntoa(((sockaddr_in *) &in_addr)->sin_addr));
					packet *recv_packet = reinterpret_cast<packet *> (&recv_buffer[0]);
					if (recv_packet->_type == ELoadStatistics) {
						packet *recv_packet = reinterpret_cast<packet *> (&recv_buffer[0]);
						statistics_payload *recv_stats = reinterpret_cast<statistics_payload *> (recv_packet->_payload);
						//printf("ELoadStatistics: count: %d  delivery time: %d ms\n", recv_stats->_packets_count, recv_stats->_delivery_time);
						assert(received_reports<MAX_REPORTS);
						delivery_time[received_reports] = recv_stats->_delivery_time;
						packet_lost[received_reports] = recv_stats->_packet_lost;
						received_reports++;
					}
				}
			}
		}

		av_delivery = mediana(delivery_time);
		av_packet_lost = mediana(packet_lost);
	}

	if(received_reports==0)
		print_log("\nStat server not responce\n");
	else
		print_log("\nUpload %f MBits/sec average delivery time: %d ms, packet lost: %.2f%%\n", (float)load_bits/1000000, av_delivery, av_packet_lost/100.00);

	return 0;

}