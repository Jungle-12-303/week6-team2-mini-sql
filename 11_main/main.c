// printf, FILE, fopen, fread 같은 입출력 함수를 사용하기 위한 헤더입니다.
#include <locale.h>
#include <stdio.h>
// malloc, free 같은 동적 메모리 함수를 사용하기 위한 헤더입니다.
#include <stdlib.h>

// SQL 문자열을 Command 구조체로 바꿔 주는 파서 인터페이스입니다.
#include "../05_parser/parser.h"
// 파싱된 Command를 실제로 실행하는 실행기 인터페이스입니다.
#include "../07_executor/executor.h"

// 파일 전체 내용을 읽어서 하나의 문자열로 반환합니다.
// 예:
//   path = "sample.sql"
//   파일 내용 = "INSERT INTO users VALUES ('kim', 20);"
//   반환값 buffer = "INSERT INTO users VALUES ('kim', 20);"
static char *read_file(const char *path) {
    // 열어 둔 파일을 가리킬 포인터입니다.
    FILE *file;
    // 파일 전체 크기를 저장합니다.
    long file_size;
    // 실제로 몇 바이트를 읽었는지 저장합니다.
    size_t read_size;
    // 파일 내용을 담아 반환할 동적 메모리 버퍼입니다.
    char *buffer;

    // 읽기 모드("r")로 파일을 엽니다.
    file = fopen(path, "r");
    // 파일 열기에 실패하면 NULL을 반환해 호출자에게 알립니다.
    if (file == NULL) {
        return NULL;
    }

    // 파일 끝으로 이동해서 전체 크기를 구할 준비를 합니다.
    if (fseek(file, 0, SEEK_END) != 0) {
        // 이동에 실패하면 파일을 닫고 실패를 반환합니다.
        fclose(file);
        return NULL;
    }

    // 현재 위치(파일 끝 오프셋)를 얻으면 파일 크기가 됩니다.
    file_size = ftell(file);
    // 크기 조회에 실패하면 음수가 들어오므로 실패 처리합니다.
    if (file_size < 0) {
        fclose(file);
        return NULL;
    }

    // 다시 파일 처음으로 돌아가 실제 읽기를 준비합니다.
    if (fseek(file, 0, SEEK_SET) != 0) {
        fclose(file);
        return NULL;
    }

    // 파일 내용 + 문자열 종료 문자('\0') 1칸까지 담을 메모리를 할당합니다.
    // 예:
    //   file_size = 38 이면 39바이트를 할당합니다.
    buffer = (char *)malloc((size_t)file_size + 1);
    // 메모리 할당 실패 시 파일을 닫고 NULL을 반환합니다.
    if (buffer == NULL) {
        fclose(file);
        return NULL;
    }

    // 파일 내용을 통째로 버퍼에 읽어옵니다.
    // 예:
    //   buffer 안에는 "INSERT INTO users VALUES ('kim', 20);" 가 들어갑니다.
    read_size = fread(buffer, 1, (size_t)file_size, file);
    // fread가 읽은 실제 길이 뒤에 문자열 종료 문자를 붙여 C 문자열로 만듭니다.
    buffer[read_size] = '\0';
    // 파일 사용이 끝났으므로 닫습니다.
    fclose(file);
    // 완성된 문자열 버퍼를 호출자에게 돌려줍니다.
    return buffer;
}

// 프로그램 시작 함수입니다.
// 전체 흐름 예시:
//   1. argv[1] = "sample.sql"
//   2. sql_text = "INSERT INTO users VALUES ('kim', 20);"
//   3. parse_sql(...) 후
//      command.type = COMMAND_INSERT
//      command.table_name = "users"
//      command.values[0] = "kim"
//      command.values[1] = "20"
//      command.value_count = 2
//   4. execute_command(...)가 03_data/users.csv에 "kim,20" 한 줄을 추가합니다.
int main(int argc, char *argv[]) {
    // SQL 파일에서 읽어 온 전체 문자열을 담을 포인터입니다.
    char *sql_text;
    // 파싱이나 실행 중 오류가 나면 사람이 읽을 수 있는 메시지를 담습니다.
    char error_message[256];
    // 파싱 결과를 담는 구조체입니다.
    Command command;

    // 현재 환경 로케일을 적용해서 한글 같은 멀티바이트 문자의 출력 폭 계산이 가능하게 합니다.
    setlocale(LC_ALL, "");

    // 실행 인자는 프로그램 이름 + SQL 파일 경로, 총 2개여야 합니다.
    // 예:
    //   정상: argc == 2, argv[1] == "sample.sql"
    //   비정상: ./mini_sql_rebuild 만 입력한 경우 argc == 1
    if (argc != 2) {
        printf("ERROR: usage: ./mini_sql_rebuild <sql_file>\n");
        return 1;
    }

    // argv[1] 경로의 파일 전체를 읽어 sql_text에 저장합니다.
    // 예:
    //   argv[1] = "sample.sql"
    //   sql_text = "SELECT * FROM users;"
    sql_text = read_file(argv[1]);
    // 파일을 못 읽었으면 경로를 포함해 오류를 출력하고 종료합니다.
    if (sql_text == NULL) {
        printf("ERROR: cannot read SQL file: %s\n", argv[1]);
        return 1;
    }

    // command를 먼저 빈 상태로 초기화합니다.
    // 초기 상태 예:
    //   command.type = COMMAND_UNKNOWN
    //   command.table_name = ""
    //   command.value_count = 0
    init_command(&command);

    // SQL 문자열을 해석해서 command 구조체를 채웁니다.
    // 예:
    //   sql_text = "INSERT INTO users VALUES ('kim', 20);"
    //   parse 후 command는 아래처럼 바뀝니다.
    //   - type: COMMAND_INSERT
    //   - table_name: "users"
    //   - values[0]: "kim"
    //   - values[1]: "20"
    //   - value_count: 2
    if (!parse_sql(sql_text, &command, error_message, sizeof(error_message))) {
        // 파싱 실패 시 왜 실패했는지 메시지를 출력합니다.
        printf("ERROR: %s\n", error_message);
        // read_file에서 malloc한 메모리를 해제합니다.
        free(sql_text);
        return 1;
    }

    // 파싱이 끝난 command를 실제로 실행합니다.
    // 예 1:
    //   command.type = COMMAND_INSERT 이면 users.csv에 한 줄 추가합니다.
    // 예 2:
    //   command.type = COMMAND_SELECT 이면 users.csv 내용을 화면에 출력합니다.
    if (!execute_command(&command, error_message, sizeof(error_message))) {
        // 실행 단계에서 실패하면 오류 메시지를 출력합니다.
        printf("ERROR: %s\n", error_message);
        // SQL 문자열 메모리를 해제합니다.
        free(sql_text);
        return 1;
    }

    // command 내부를 다시 초기 상태로 돌립니다.
    free_command(&command);
    // 파일에서 읽어 온 SQL 문자열 메모리를 해제합니다.
    free(sql_text);
    // 정상 종료를 의미하는 0을 반환합니다.
    return 0;
}
