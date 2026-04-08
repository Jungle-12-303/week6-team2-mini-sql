// 프로젝트 전체가 같이 쓰는 공용 약속 파일 
// 
#ifndef COMMON_H
#define COMMON_H

#include <stddef.h>
#include <stdio.h>

// sql 파일을 읽을 최대 길이 
#define MAX_SQL_LENGTH 1024
// 테이블명 최대 길이 
#define MAX_TABLE_NAME 64
// 한 insert에서 받을 최대 값 개수 
#define MAX_VALUES 16
// 값 하나의 최대 길이 
#define MAX_VALUE_LENGTH 128
// csv 한줄 최대 길이 
#define MAX_LINE_LENGTH 1024
// data/users.csv 같은 경로 최대길이? 파일의 이름의 길이라는거야 아니면 depth 파일 깊이 길이라는거야 
#define MAX_PATH_LENGTH 256
// 에러 메시지 버퍼 크기 버퍼가 뭐지
#define MAX_ERROR_LENGTH 256
// 기본 데이터 폴더 이름 
#define DEFAULT_DATA_DIR "data"


// INSERT인지 SELECT인지 구분을 쉽게 하기위해 만듬 
// enum을 사용하지 않으면 우리가 0은 insert고 1은 select라고 정헤야하는데 다른사람이 보기 힘드니까 enum을 사용함? 
// STATEMENT_INSERT는 사실 0이고 STATEMENT_SELECT는 사실 1 임
typedef enum StatementType {
    STATEMENT_INSERT,
    STATEMENT_SELECT
} StatementType;

typedef struct Statement {
    StatementType type;
    char table_name[MAX_TABLE_NAME];
    int value_count;
    char values[MAX_VALUES][MAX_VALUE_LENGTH];
} Statement;

#endif


