
#include "client_lib.h"


void to_print(char const* str) {
	printf("%s\n", str);
//	fflush(stdout);
}

int main(int argc, char *argv[]) {

	client_lib inst(&to_print);
	return inst.run_test("54.164.119.210");
	//return inst.run_test("127.0.0.1");
}
