//
// Created by Oleg Golosovskiy on 08/10/2018.
//

#ifndef SPEED_TEST_CLIENT_LIB_H
#define SPEED_TEST_CLIENT_LIB_H

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

#include <fcntl.h>
#include <assert.h>

typedef void to_print_callback(char const* str);

class client_lib {
public:
	client_lib(to_print_callback* log);
	 // 0 success, -1 error
	int run_test(char const* server);
};


#endif //SPEED_TEST_CLIENT_LIB_H
