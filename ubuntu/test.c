#include <stdio.h>

struct test_struct{
	int test_int;
	char *test_charp;
};

void function_void_return_no_arg();
int function_int_return_int_long_float_charp_arg(int a, long b, float c, char *d);
char* function_charp_return_int_long_float_charp_arg(int a, long b, float c, char *d);

void function_void_return_no_arg(){
	printf("STDOUT::void function_void_return_no_arg()\n");
}

int function_int_return_int_long_float_charp_arg(int a, long b, float c, char *d){
	int result;
	result = 101;
	printf("STDOUT::%d function_int_return_int_long_float_charp_arg(a=%d, b=%ld, c=%f, d=%s)\n", result, a, b, c, d);
	return result;
}

char* function_charp_return_int_long_float_charp_arg(int a, long b, float c, char *d){
	char *result;
	result = "hello";
	printf("STDOUT::%s function_int_return_int_long_float_charp_arg(a=%d, b=%ld, c=%f, d=%s)\n", result, a, b, c, d);
	return result;
}

int main(int argc, char *argv[]){
	int resultmain;
	int resultint;
	char *resultcharp;
	int i;
	struct test_struct temp_test_struct;

	resultmain = 0;

	printf("STDOUT::%d main(argc=%d", resultmain, argc-1);
	for(i = 1; i < argc; i++){
		printf(", argv[%d]=%s", i-1, argv[i]);
	}
	printf(")\n");

	function_void_return_no_arg();
	resultint = function_int_return_int_long_float_charp_arg(901, 902, 903.14, "nine-o-four");
	resultcharp = function_charp_return_int_long_float_charp_arg(801, 802, 803.14, "eight-o-four");

	temp_test_struct.test_int = 701;
	temp_test_struct.test_charp = "seven-o-two";

	printf("STDOUT:struct test_struct{test_int=%d, test_charp=%s}\n", temp_test_struct.test_int, temp_test_struct.test_charp);

	return 0;
}
