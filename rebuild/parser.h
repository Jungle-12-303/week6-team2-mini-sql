// 파서를 바깥에서 어떻게 사용할지 선언하는 파일

#ifndef PARSER_H
#define PARSER_H

#include "common.h"

// 파서는 이런 입력을받는다 .h 파일에 정의 main함수에서 컴파일 할때 함수를 사용하기위해 정의되어있는 함수를 컴파일 할수있도록 

/*
순서를 보면:

main.c가 #include "parser.h"를 함
이때 들어오는 건 함수 선언뿐입니다.
int parse_sql(const char *sql, Statement *stmt, char *error, size_t error_size);
컴파일러는 main.c를 보면서
“parse_sql이라는 함수가 있고, 인자/반환형은 이렇구나”까지만 압니다.
그래서 main.c 안의 호출 코드를 만들 수 있습니다.

동시에 parser.c도 따로 컴파일됩니다.
여기에 parse_sql(...) { ... } 실제 구현이 있습니다.

마지막에 링커(linker) 가

main.c 쪽의 “parse_sql 호출”
parser.c 쪽의 “parse_sql 실제 구현”
이 둘을 연결합니다.

즉:

parser.h = 함수가 있다는 약속
parser.c = 함수의 실제 몸체
링커 = 둘을 이어 붙이는 사람
*/ 

int parse_sql(const char *sql, Statement *stmt, char *error, size_t error_size);

#endif
