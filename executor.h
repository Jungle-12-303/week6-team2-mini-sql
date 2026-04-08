#ifndef EXECUTOR_H
#define EXECUTOR_H

#include "common.h"

// const Statement *stmt: 왜 필요하냐면 stmt안의 타입이 insert인지, select인지, 대상테이블(ex)users)이 뭔지 , insert면 값이 몇개인지, insert면 값들이 뭔지 
// const char *data_dir: 왜 필요하냐면 storage는 나중에 이런 경로를 만들어야 하기 때문입니다.
// const 가 붙은 이유 : execute는 파서가 정의한 구조체대로 어떤 storage행동 (넣을건지, 탐색할건지)을 "읽기만"하고 판단하기때문
// const char *data_dir : 이건 데이터 파일들이 들어 있는 폴더가 어디인지 알려줍니다.

// char *error: 실패했을 때 이유를 써 넣을 버퍼입니다.
// 5. size_t error_size 이건 error 버퍼 크기입니다. C에서는 버퍼 크기를 모르고 문자열을 쓰면 메모리 오염 위험이 큽니다.


int execute_statement(const Statement *stmt,
                      const char *data_dir,
                      FILE *out,
                      char *error,
                      size_t error_size);

#endif
