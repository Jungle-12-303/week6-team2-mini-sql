#include "common.h"   // Statement, MAX_SQL_LENGTH, DEFAULT_DATA_DIR 같은 공통 타입/상수를 가져온다.
#include "executor.h" // execute_statement() 선언을 가져와서 main이 실행 단계로 넘길 수 있게 한다.
#include "parser.h"   // parse_sql() 선언을 가져와서 SQL 문자열을 구조체로 해석할 수 있게 한다.

#include <stdio.h>    // fopen, fread, fclose, fprintf, snprintf, stdout, stderr를 쓰기 위해 포함한다.
#include <string.h>   // 이 파일에서는 직접 쓰는 문자열 함수는 거의 없지만 현재 포함되어 있다.

static int read_sql_file(const char *path,
                         char *buffer,
                         size_t buffer_size,
                         char *error,
                         size_t error_size) {
    FILE *file = fopen(path, "r");         // path가 가리키는 SQL 파일을 읽기 모드로 연다. 실패하면 NULL을 돌려준다.
    size_t read_size;                      // fread()가 실제로 몇 바이트를 읽었는지 저장할 변수다.

    if (file == NULL) {                    // 파일을 열 수 없으면 이후 읽기 작업을 하면 안 되므로 바로 실패 처리한다.
        snprintf(error, error_size, "Cannot open SQL file: %s", path); // 왜 실패했는지 호출자(main)가 출력할 메시지를 만든다.
        return 0;                          // 0은 실패를 뜻한다.
    }

    read_size = fread(buffer, 1, buffer_size - 1, file); // 문자열 끝 문자 '\0'를 넣을 자리를 남기기 위해 최대 한 칸 덜 읽는다.
    if (ferror(file)) {                    // fread() 도중 디스크/입출력 오류가 났는지 확인한다.
        fclose(file);                      // 이미 연 파일이 있으므로 실패 경로에서도 반드시 닫아 준다.
        snprintf(error, error_size, "Cannot read SQL file: %s", path); // 읽기 실패 이유를 에러 버퍼에 기록한다.
        return 0;                          // 읽기 실패를 호출자에게 알린다.
    }

    if (read_size == buffer_size - 1 && fgetc(file) != EOF) { // 버퍼를 꽉 채웠는데도 파일이 안 끝났다면 이 SQL은 버퍼보다 크다.
        fclose(file);                      // 더 이상 읽지 않고 파일을 닫는다.
        snprintf(error, error_size, "SQL file is too large: %s", path); // "파일이 너무 크다"는 구체적인 실패 이유를 남긴다.
        return 0;                          // 버퍼 제한을 넘었으므로 실패 처리한다.
    }

    buffer[read_size] = '\0';              // fread()는 문자열 끝 문자를 넣어 주지 않으므로 C 문자열로 쓰려면 직접 붙여야 한다.
    fclose(file);                          // 정상적으로 읽었어도 파일 자원은 닫아야 한다.
    return 1;                              // 여기까지 왔으면 파일 읽기가 성공했다는 뜻이다.
}

int main(int argc, char *argv[]) {
    char sql[MAX_SQL_LENGTH];              // SQL 파일 전체 내용을 메모리에 담아 둘 버퍼다.
    char error[MAX_ERROR_LENGTH];          // 실패 이유를 사람이 읽을 수 있는 문자열로 담아 둘 버퍼다.
    Statement stmt;                        // 파서가 SQL을 해석한 결과를 채워 넣을 구조체 변수다.

    if (argc != 2) {                       // 실행 파일 이름 + SQL 파일 경로, 이렇게 정확히 2개의 인자가 와야 한다.
        fprintf(stderr, "Usage: %s <query.sql>\n", argv[0]); // 사용법을 표준 에러로 출력해 사용자가 실행 형식을 바로 알 수 있게 한다.
        return 1;                          // 인자 형식이 틀렸으므로 비정상 종료 코드를 반환한다.
    }
   
    error[0] = '\0';                       // 아직 에러 메시지가 없다는 뜻으로 빈 문자열 상태로 초기화한다.
    if (!read_sql_file(argv[1], sql, sizeof(sql), error, sizeof(error))) { // argv[1]의 SQL 파일을 읽어서 sql 버퍼에 넣는다.
        fprintf(stderr, "Error: %s\n", error); // read_sql_file()이 써 준 실패 이유를 그대로 사용자에게 보여 준다.
        return 1;                          // 파일 읽기에 실패했으므로 이후 파싱/실행 단계로 가지 않는다.
    }

    if (!parse_sql(sql, &stmt, error, sizeof(error))) { // sql 문자열을 읽어 stmt 구조체 안에 "무슨 문장인지"를 구조화해서 채운다.
        fprintf(stderr, "Error: %s\n", error); // 문법 오류나 지원하지 않는 SQL이면 그 이유를 출력한다.
        return 1;                          // 파싱에 실패했으니 실행 단계로 넘기면 안 된다.
    }

    if (!execute_statement(&stmt, DEFAULT_DATA_DIR, stdout, error, sizeof(error))) { // 파싱된 명령을 data 디렉터리를 기준으로 실제 실행한다.
        fprintf(stderr, "Error: %s\n", error); // 실행 중 생긴 오류(예: 파일 없음, 중복 id, 컬럼 불일치)를 사용자에게 출력한다.
        return 1;                          // 실행 실패도 비정상 종료로 처리한다.
    }

    return 0;                              // 파일 읽기, 파싱, 실행이 모두 성공했으므로 정상 종료한다.
}
