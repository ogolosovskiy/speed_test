//
// Created by Oleg Golosovskiy on 04/10/2018.
//

#ifndef SPEED_TEST_PACKET_H
#define SPEED_TEST_PACKET_H

#include <memory.h>
#include <random>
#include <chrono>

// server listen on
const int SERVER_PORT = 3010;

// one ELoadStatistics packet will be send for every N received ELoad's packets
#define REPORT_STATISTICS_FOR_PACKETS 100

// How many ETimeSync packets will be send to measure RTT / and clock synchronization
#define RTT_MAX_ATTEMPTS 8

// how much packets will be send per second
int const packets_per_seconds = 4;

// bits send every second
int const load_bits = 1500000;

// max size of client's pool for received reports
int const MAX_REPORTS = 1000;

// how much load's sets will be send, every set has load_bits size
// one load's set sends every second
int const LOAD_SERIES = 10;

// max size of ELoadStatistics packet
int const load_statistic_buffer = 2048;

#define ZERO_RESET(m) memset(&m, 0, sizeof(m))
#define NEGATIVE_RESET(m) memset(&m,0xFF, sizeof(m))

enum test_type {
	  EUnknown = 0
	, EEcho = 1 // means that server echo this packet
	, ETimeSync = 2 // server reply on this packet and add time stamp to it
	, ELoad = 3	// regular load packet
	, ELoadStatistics = 4  // server's statistics
};

const int PAYLOAD_SIZE = 1000;

struct packet {
	int _version;
	test_type _type;
	int _test_id;
	char _payload[PAYLOAD_SIZE];
	packet() {
		clear();
	}
	void clear()
	{
		memset(this, 0, sizeof(packet));
		_version = 1;
		std::random_device rd;  //Will be used to obtain a seed for the random number engine
		std::mt19937 gen(rd()); //Standard mersenne_twister_engine seeded with rd()
		std::uniform_int_distribution<> uid(0, std::numeric_limits<int>::max());
		_test_id = uid(gen);

	}
};

// ELoad payload
struct load_payload {
	long _set_start_time_stamp;
	long _seqence_number;
	long _load_set_count;
	long _load_set_packets;
};

// ETimeSync payload
struct time_sync_payload {
	long _client_time_stamp;
	long _server_time_stamp;
};

struct statistics_payload {
	int _packets_count;
	int _delivery_time; // ms
	int _packet_lost; // percents * 100
};

inline void set_min(long& old_val, long& new_val) {
	if(old_val == -1)
		old_val = new_val;
	else
		old_val = std::min(old_val, new_val);
}

inline void set_max(long& old_val, long& new_val) {
	if(old_val == -1)
		old_val = new_val;
	else
		old_val = std::max(old_val, new_val);
}

template<typename T, size_t N>
int mediana( T (&array)[N] )
{
	size_t elements = N;
	int acc = 0;
	int real_count = 0;
	for (int i = 0; i < elements; ++i) {
		if(array[i]!=-1) {
			acc += array[i];
			++real_count;
		}
	}
	if(real_count)
		return acc / real_count;
	else
		return 0;
}

inline void add_server_time_stamp(packet* recv_packet)
{
	std::chrono::time_point<std::chrono::system_clock> now;
	now = std::chrono::system_clock::now();
	time_sync_payload* payload = reinterpret_cast<time_sync_payload*>(&recv_packet->_payload[0]);
	payload->_server_time_stamp = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count();
}


#endif //SPEED_TEST_PACKET_H
