#ifndef STORAGE_H
#define STORAGE_H

#include "common.h"


/*
넣지 않은 게 아니라, 별도 함수로 분리하지 않고 기존 storage_select_all()에 흡수한 겁니다. 다만 함수명은 제가 정리 안 해서 지금 이름이 틀린 상태에 가깝습니다.

현재 구조는:

파서가 SELECT *인지, SELECT name, id인지 Statement에 담고
스토리지는 그 Statement를 받아 같은 함수 안에서 처리합니다
즉 API 관점에서는

SELECT 전체 조회
SELECT 특정 열 조회
를 굳이 두 함수로 나누지 않고, 하나의 SELECT 실행 함수로 묶은 설계입니다.
*/


int storage_append_row(const char *data_dir,
                       const Statement *stmt,
                       char *error,
                       size_t error_size);



// 그니까 select * 아니면 특정열 조회한다 라고 조건이 되어있는건가
int storage_select_all(const char *data_dir,
                       const Statement *stmt,
                       FILE *out,
                       char *error,
                       size_t error_size);

#endif
