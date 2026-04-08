#include "storage.h"   // storage 함수 선언과 공용 상수/Statement 타입을 쓰기 위해 포함한다.

#include <ctype.h>     // isspace 를 쓰기 위해 포함한다.
#include <stdio.h>     // snprintf, fputs, fputc 같은 입출력 함수를 쓰기 위해 포함한다.
#include <string.h>    // strlen, memcpy 를 쓰기 위해 포함한다.

// storage 단계에서 공통으로 에러 문자열을 쓰기 위한 보조 함수다.
static void set_error(char *error, size_t error_size, const char *message) {
    if (error == NULL || error_size == 0) {                 // 에러 버퍼가 없거나 크기가 0이면
        return;                                             // 쓸 곳이 없으니 조용히 종료한다.
    }

    snprintf(error, error_size, "%s", message);             // 버퍼 크기를 넘지 않게 메시지를 복사한다.
}

// data_dir 과 table_name 을 합쳐서 실제 CSV 파일 경로를 만든다.
// 예: "data" + "users" -> "data/users.csv"
static int build_table_path(const char *data_dir,
                            const char *table_name,
                            char *path,
                            size_t path_size,
                            char *error,
                            size_t error_size) {
    int written = snprintf(path,                            // 결과를 path 버퍼에 쓰고
                           path_size,                       // path 버퍼 크기만큼만 사용하면서
                           "%s/%s.csv",                    // "폴더/테이블이름.csv" 형식으로
                           data_dir,                        // 폴더 이름을 넣고
                           table_name);                     // 테이블 이름을 넣는다.

    if (written < 0 || (size_t)written >= path_size) {     // snprintf 실패 또는 버퍼 초과면
        set_error(error, error_size, "Table path is too long");  // 왜 실패했는지 기록하고
        return 0;                                          // 실패를 반환한다.
    }

    return 1;                                              // 경로 생성 성공
}

// fgets 로 읽은 줄 끝에 붙을 수 있는 \n, \r 을 제거한다.
static void strip_line_end(char *line) {
    size_t length = strlen(line);                          // 현재 문자열 길이를 구한다.

    while (length > 0 &&                                   // 아직 문자가 남아 있고
           (line[length - 1] == '\n' ||                    // 마지막 문자가 개행이거나
            line[length - 1] == '\r')) {                   // 캐리지리턴이면
        line[length - 1] = '\0';                           // 그 문자를 문자열 끝으로 덮어써서 제거한다.
        length--;                                          // 길이도 한 칸 줄인다.
    }
}

// 줄이 공백만으로 이루어졌는지 확인한다.
static int line_is_blank(const char *line) {
    while (*line != '\0') {                                // 문자열 끝까지 한 글자씩 본다.
        if (!isspace((unsigned char)*line)) {              // 공백이 아닌 글자를 하나라도 만나면
            return 0;                                      // 빈 줄이 아니다.
        }

        line++;                                            // 다음 글자로 이동한다.
    }

    return 1;                                              // 끝까지 공백뿐이면 빈 줄이다.
}

// CSV 한 줄에 컬럼이 몇 개인지 쉼표 개수로 센다.
// 예: "id,name,age" -> 3
static int count_csv_columns(const char *line) {
    char copy[MAX_LINE_LENGTH];                            // 원본 line 을 건드리지 않기 위한 복사 버퍼
    int columns = 1;                                       // 쉼표가 0개여도 컬럼은 1개라고 가정하고 시작 쉼표가 하나도 없어도 최소한 데이터 1개(id)는 있을 테니까 1부터 시작합니다.
    size_t index;                                          // 문자열 순회용 인덱스
    size_t length = strlen(line);                          // 입력 줄 길이

    if (length >= sizeof(copy)) {                          // 복사 버퍼보다 길면 안전하게 처리할 수 없으니
        return 0;                                          // 잘못된 줄로 보고 0을 반환한다.
    }

    // strlen은 '\0'을 포함하지 않은 길이를 알려주기때문에 +1해서 
    memcpy(copy, line, length + 1);                        // line 전체를 copy 로 복사한다.
    strip_line_end(copy);                                  // 줄 끝 \n, \r 을 제거해서 순수 CSV 내용만 남긴다.

    if (copy[0] == '\0') {                                 // 줄 끝 제거 후 비어 있으면
        return 0;                                          // 유효한 CSV 헤더/데이터가 아니라고 본다.
    }

    for (index = 0; copy[index] != '\0'; index++) {       // 문자열 끝까지 한 글자씩 보면서
        if (copy[index] == ',') {                          // 쉼표를 만날 때마다
            columns++;                                     // 컬럼 개수를 1 늘린다.
        }
    }

    return columns;                                        // 최종 컬럼 개수를 반환한다.
}

// 출력 대상 스트림에 한 줄을 쓰되, 마지막 줄바꿈이 없으면 직접 붙여 준다.
static void write_csv_line(FILE *out, const char *line) {
    size_t length = strlen(line);                          // 현재 줄 길이를 구한다.

    fputs(line, out);                                      // 줄 내용을 먼저 그대로 출력한다.
    if (length == 0 || line[length - 1] != '\n') {        // 줄이 비었거나 마지막이 개행이 아니면
        fputc('\n', out);                                  // 개행을 하나 직접 붙여 출력 형식을 맞춘다.
    }
}
