// 공백 판별, 문자 종류 판별, 대소문자 변환 함수들을 사용하기 위한 헤더입니다.
#include <ctype.h>
// snprintf 같은 문자열 포맷 함수들을 사용하기 위한 헤더입니다.
#include <stdio.h>
// strlen, strcpy, memcpy, memmove 같은 문자열/메모리 함수를 사용하기 위한 헤더입니다.
#include <string.h>

// 공통 상수와 자료구조(Command 등)를 가져옵니다.
#include "../04_common/common.h"
// 파서 인터페이스 선언을 가져옵니다.
#include "../05_parser/parser.h"

// 현재 커서가 가리키는 위치에서 공백을 모두 건너뛰는 함수입니다.
static void skip_spaces(const char **text) {
    // 문자열이 끝나지 않았고 현재 문자가 공백이면 계속 앞으로 이동합니다.
    while (**text != '\0' && isspace((unsigned char)**text)) {
        // 포인터가 가리키는 실제 위치를 한 칸 전진시킵니다.
        (*text)++;
    }
}

// 테이블 이름 같은 식별자에 들어갈 수 있는 문자인지 검사합니다.
static int is_identifier_char(char ch) {
    // 영문/숫자이거나 밑줄(_)이면 식별자 문자로 인정합니다.
    return isalnum((unsigned char)ch) || ch == '_';
}

// 두 문자를 대소문자 구분 없이 비교합니다.
static int chars_equal_ignore_case(char left, char right) {
    // 둘 다 소문자로 바꾼 뒤 같은지 비교합니다.
    return tolower((unsigned char)left) == tolower((unsigned char)right);
}

// 현재 학습용 프로젝트에서 지원하는 테이블인지 검사합니다.
static int is_supported_table(const char *table_name) {
    return strcmp(table_name, "materials") == 0;
}

// text가 keyword로 시작하는지 대소문자 구분 없이 검사합니다.
static int starts_with_ignore_case(const char *text, const char *keyword) {
    // 키워드 문자열을 끝까지 한 글자씩 비교합니다.
    while (*keyword != '\0') {
        // text가 먼저 끝나거나 현재 글자가 다르면 시작 문자열이 아닙니다.
        if (*text == '\0' || !chars_equal_ignore_case(*text, *keyword)) {
            // 불일치이므로 실패를 반환합니다.
            return 0;
        }
        // 다음 문자 비교를 위해 text를 한 칸 이동합니다.
        text++;
        // 키워드도 다음 문자로 이동합니다.
        keyword++;
    }
    // 키워드 끝까지 모두 일치했으므로 성공입니다.
    return 1;
}

// 문자열 앞뒤 공백을 제거하고 결과를 같은 버퍼 안에 유지합니다.
static void trim_in_place(char *text) {
    // 앞쪽 공백을 건너뛴 실제 시작 위치를 가리킬 포인터입니다.
    char *start = text;
    // 뒤쪽 공백을 지울 때 사용할 끝 포인터입니다.
    char *end;
    // 현재 문자열 길이를 저장합니다.
    size_t length;

    // 앞쪽 공백을 모두 건너뛰어 내용이 시작하는 위치를 찾습니다.
    while (*start != '\0' && isspace((unsigned char)*start)) {
        // 첫 비공백 문자를 만날 때까지 이동합니다.
        start++;
    }

    // 원래 시작 위치와 실제 내용 시작 위치가 다르면 앞쪽 공백이 있었다는 뜻입니다.
    if (start != text) {
        // 내용 전체를 앞으로 당겨서 버퍼 맨 앞에 정렬합니다.
        memmove(text, start, strlen(start) + 1);
    }

    // 앞쪽 정리가 끝난 뒤 현재 문자열 길이를 다시 구합니다.
    length = strlen(text);
    // 빈 문자열이면 뒤쪽 공백을 지울 것도 없으므로 종료합니다.
    if (length == 0) {
        return;
    }

    // 문자열의 마지막 문자 위치부터 뒤쪽 공백 제거를 시작합니다.
    end = text + length - 1;
    // 끝에서부터 공백을 만나는 동안 널 문자로 바꿔 잘라냅니다.
    while (end >= text && isspace((unsigned char)*end)) {
        // 현재 공백 문자를 문자열 종료 문자로 바꿉니다.
        *end = '\0';
        // 이전 문자로 이동합니다.
        end--;
    }
}

// 현재 위치에서 지정한 키워드를 소비하고, 맞으면 포인터를 그 뒤로 이동합니다.
static int consume_keyword(const char **text, const char *keyword) {
    // 키워드 길이를 저장해 둘 변수입니다.
    size_t keyword_length;

    // 키워드 앞의 공백은 무시합니다.
    skip_spaces(text);
    // 비교와 포인터 이동에 쓸 키워드 길이를 계산합니다.
    keyword_length = strlen(keyword);

    // 현재 위치가 해당 키워드로 시작하지 않으면 실패입니다.
    if (!starts_with_ignore_case(*text, keyword)) {
        return 0;
    }

    // 키워드 뒤에 식별자 문자가 오면 INSERTX 같은 경우라서 진짜 키워드가 아닙니다.
    if (is_identifier_char((*text)[keyword_length])) {
        return 0;
    }

    // 키워드를 정상적으로 읽었으므로 현재 위치를 키워드 뒤로 이동합니다.
    *text += keyword_length;
    // 성공을 반환합니다.
    return 1;
}

// 현재 위치에서 테이블 이름을 읽어 table_name 버퍼에 저장합니다.
static int parse_table_name(const char **text, char *table_name, int table_size) {
    // 저장한 문자 수를 셉니다.
    int length = 0;

    // 테이블 이름 앞의 공백은 무시합니다.
    skip_spaces(text);

    // 첫 글자가 식별자 문자가 아니면 유효한 테이블 이름이 아닙니다.
    if (!is_identifier_char(**text)) {
        return 0;
    }

    // 식별자 문자가 이어지는 동안 테이블 이름을 한 글자씩 복사합니다.
    while (**text != '\0' && is_identifier_char(**text)) {
        // 널 종료 문자까지 고려했을 때 버퍼를 넘치면 실패합니다.
        if (length + 1 >= table_size) {
            return 0;
        }
        // 현재 문자를 결과 버퍼에 저장합니다.
        table_name[length] = **text;
        // 다음 저장 위치로 이동합니다.
        length++;
        // 입력 포인터도 다음 문자로 이동합니다.
        (*text)++;
    }

    // 복사한 테이블 이름 끝에 문자열 종료 문자를 붙입니다.
    table_name[length] = '\0';
    // 최소 한 글자 이상 읽었으면 성공입니다.
    return length > 0;
}

// 값이 작은따옴표나 큰따옴표로 감싸져 있으면 양쪽 따옴표를 제거합니다.
static void strip_quotes(char *text) {
    // 현재 문자열 길이를 구합니다.
    size_t length = strlen(text);

    // 앞뒤 따옴표를 검사하려면 최소 2글자는 필요합니다.
    if (length >= 2) {
        // 양쪽이 모두 작은따옴표이거나 모두 큰따옴표인 경우만 제거합니다.
        if ((text[0] == '\'' && text[length - 1] == '\'') ||
            (text[0] == '"' && text[length - 1] == '"')) {
            // 맨 앞 따옴표를 제거하기 위해 한 칸 앞으로 당깁니다.
            memmove(text, text + 1, length - 2);
            // 맨 뒤 따옴표 자리에 널 종료 문자를 넣어 문자열을 끝냅니다.
            text[length - 2] = '\0';
        }
    }
}

// VALUES 목록에서 잘라낸 개별 값을 Command 구조체에 추가합니다.
static int add_value(Command *command, const char *start, int length, char *error_message, int error_size) {
    // 원본 문자열 일부를 임시로 복사해 다듬기 위한 버퍼입니다.
    char buffer[128];

    // 저장 가능한 최대 값 개수를 넘으면 실패합니다.
    if (command->value_count >= MAX_VALUES) {
        snprintf(error_message, error_size, "too many values in INSERT");
        return 0;
    }

    // 임시 버퍼 크기를 넘는 값은 복사할 수 없으므로 실패합니다.
    if (length >= (int)sizeof(buffer)) {
        snprintf(error_message, error_size, "value is too long");
        return 0;
    }

    // 잘라낸 값 구간을 임시 버퍼로 복사합니다.
    memcpy(buffer, start, (size_t)length);
    // 복사한 문자열 끝에 널 종료 문자를 붙입니다.
    buffer[length] = '\0';
    // 값 주변의 공백을 제거합니다.
    trim_in_place(buffer);
    // 값이 따옴표로 감싸져 있으면 따옴표를 제거합니다.
    strip_quotes(buffer);

    // Command 내부 각 값 버퍼 크기를 넘는지도 한 번 더 검사합니다.
    if (strlen(buffer) >= sizeof(command->values[0])) {
        snprintf(error_message, error_size, "value is too long");
        return 0;
    }

    // 정리된 값을 현재 value_count 위치에 저장합니다.
    strcpy(command->values[command->value_count], buffer);
    // 다음 값을 받을 수 있도록 개수를 증가시킵니다.
    command->value_count++;
    // 값 추가 성공을 반환합니다.
    return 1;
}

// VALUES (...) 내부를 파싱해서 쉼표로 구분된 값들을 추출합니다.
static int parse_values_list(const char *text, Command *command, char *error_message, int error_size) {
    // 입력을 따라가며 읽을 커서입니다.
    const char *cursor = text;
    // 현재 값이 시작한 위치를 저장합니다.
    const char *value_start;
    // 작은따옴표 문자열 내부인지 추적합니다.
    int in_single_quote = 0;
    // 큰따옴표 문자열 내부인지 추적합니다.
    int in_double_quote = 0;

    // VALUES 뒤 공백은 무시합니다.
    skip_spaces(&cursor);
    // VALUES 다음은 반드시 '(' 로 시작해야 합니다.
    if (*cursor != '(') {
        snprintf(error_message, error_size, "INSERT must contain VALUES (...)");
        return 0;
    }
    // '(' 다음 문자부터 실제 값 목록이 시작됩니다.
    cursor++;
    // 첫 번째 값의 시작 위치를 기록합니다.
    value_start = cursor;

    // 문자열 끝까지 한 글자씩 보며 값 구분자를 찾습니다.
    while (*cursor != '\0') {
        // 큰따옴표 안이 아닐 때 작은따옴표를 만나면 상태를 토글합니다.
        if (*cursor == '\'' && !in_double_quote) {
            in_single_quote = !in_single_quote;
        // 작은따옴표 안이 아닐 때 큰따옴표를 만나면 상태를 토글합니다.
        } else if (*cursor == '"' && !in_single_quote) {
            in_double_quote = !in_double_quote;
        // 따옴표 바깥의 쉼표는 값 구분자로 처리합니다.
        } else if (*cursor == ',' && !in_single_quote && !in_double_quote) {
            // 직전 값 구간을 잘라 Command에 추가합니다.
            if (!add_value(command, value_start, (int)(cursor - value_start), error_message, error_size)) {
                return 0;
            }
            // 다음 값은 쉼표 다음 위치에서 시작합니다.
            value_start = cursor + 1;
        // 따옴표 바깥의 ')' 는 VALUES 목록의 끝입니다.
        } else if (*cursor == ')' && !in_single_quote && !in_double_quote) {
            // 마지막 값도 잘라서 추가합니다.
            if (!add_value(command, value_start, (int)(cursor - value_start), error_message, error_size)) {
                return 0;
            }
            // ')' 다음으로 이동합니다.
            cursor++;
            // 뒤쪽 공백을 건너뜁니다.
            skip_spaces(&cursor);
            // 닫는 괄호 뒤에 다른 문자가 남아 있으면 문법 오류입니다.
            if (*cursor != '\0') {
                snprintf(error_message, error_size, "unexpected text after INSERT statement");
                return 0;
            }
            // 최소 하나 이상의 값을 읽었는지 확인하며 성공을 반환합니다.
            return command->value_count > 0;
        }
        // 다음 문자로 이동해 계속 파싱합니다.
        cursor++;
    }

    // 문자열 끝까지 갔는데 ')'를 못 만났다면 괄호가 닫히지 않은 오류입니다.
    snprintf(error_message, error_size, "missing closing ')' in VALUES list");
    return 0;
}

// INSERT 문 전체를 파싱합니다.
static int parse_insert(const char *sql, Command *command, char *error_message, int error_size) {
    // 입력 문자열을 따라가며 읽을 커서입니다.
    const char *cursor = sql;

    // INSERT 키워드가 맨 앞에 와야 합니다.
    if (!consume_keyword(&cursor, "INSERT")) {
        snprintf(error_message, error_size, "INSERT statement must start with INSERT");
        return 0;
    }

    // INSERT 다음에는 INTO가 와야 합니다.
    if (!consume_keyword(&cursor, "INTO")) {
        snprintf(error_message, error_size, "INSERT statement must contain INTO");
        return 0;
    }

    // INTO 뒤에서 테이블 이름을 읽어 command에 저장합니다.
    if (!parse_table_name(&cursor, command->table_name, MAX_TABLE_NAME_LENGTH)) {
        snprintf(error_message, error_size, "invalid table name in INSERT");
        return 0;
    }

    // 현재 구현은 materials 테이블만 지원합니다.
    if (!is_supported_table(command->table_name)) {
        snprintf(error_message, error_size, "only materials table is supported");
        return 0;
    }

    // 테이블 이름 뒤에는 VALUES 키워드가 와야 합니다.
    if (!consume_keyword(&cursor, "VALUES")) {
        snprintf(error_message, error_size, "INSERT statement must contain VALUES");
        return 0;
    }

    // 이 명령은 INSERT 타입이라고 기록합니다.
    command->type = COMMAND_INSERT;
    // VALUES 목록 파싱 결과를 그대로 반환합니다.
    return parse_values_list(cursor, command, error_message, error_size);
}

// SELECT 문 전체를 파싱합니다.
static int parse_select(const char *sql, Command *command, char *error_message, int error_size) {
    // 입력 문자열을 따라가며 읽을 커서입니다.
    const char *cursor = sql;

    // SELECT 키워드가 맨 앞에 와야 합니다.
    if (!consume_keyword(&cursor, "SELECT")) {
        snprintf(error_message, error_size, "SELECT statement must start with SELECT");
        return 0;
    }

    // SELECT 뒤 공백을 건너뜁니다.
    skip_spaces(&cursor);
    // 현재 구현은 SELECT * 만 지원합니다.
    if (*cursor != '*') {
        snprintf(error_message, error_size, "only SELECT * is supported");
        return 0;
    }
    // '*' 다음 위치로 이동합니다.
    cursor++;

    // 그 다음에는 FROM 키워드가 나와야 합니다.
    if (!consume_keyword(&cursor, "FROM")) {
        snprintf(error_message, error_size, "SELECT statement must contain FROM");
        return 0;
    }

    // FROM 뒤 테이블 이름을 읽어 command에 저장합니다.
    if (!parse_table_name(&cursor, command->table_name, MAX_TABLE_NAME_LENGTH)) {
        snprintf(error_message, error_size, "invalid table name in SELECT");
        return 0;
    }

    // 현재 구현은 materials 테이블만 지원합니다.
    if (!is_supported_table(command->table_name)) {
        snprintf(error_message, error_size, "only materials table is supported");
        return 0;
    }

    // 테이블 이름 뒤 공백을 건너뜁니다.
    skip_spaces(&cursor);
    // 그 뒤에 다른 문자가 남아 있으면 아직 지원하지 않는 문법입니다.
    if (*cursor != '\0') {
        snprintf(error_message, error_size, "unexpected text after SELECT statement");
        return 0;
    }

    // 이 명령은 SELECT 타입이라고 기록합니다.
    command->type = COMMAND_SELECT;
    // SELECT 파싱 성공입니다.
    return 1;
}

// Command 구조체를 안전한 초기 상태로 되돌립니다.
void init_command(Command *command) {
    // 반복문에서 사용할 인덱스 변수입니다.
    int index;

    // 아직 어떤 명령인지 모르는 상태로 초기화합니다.
    command->type = COMMAND_UNKNOWN;
    // 테이블 이름을 빈 문자열로 초기화합니다.
    command->table_name[0] = '\0';
    // 저장된 값 개수도 0으로 초기화합니다.
    command->value_count = 0;

    // 모든 값 버퍼를 빈 문자열로 초기화합니다.
    for (index = 0; index < MAX_VALUES; index++) {
        // 각 값 문자열의 첫 글자를 널 문자로 만들어 비웁니다.
        command->values[index][0] = '\0';
    }
}

// 현재 구조에서는 동적 할당이 없어서 초기화만 다시 수행하면 됩니다.
void free_command(Command *command) {
    // 실제 해제 대신 초기 상태로 되돌립니다.
    init_command(command);
}

// SQL 문자열 전체를 받아 명령 종류에 따라 적절한 파서로 분기합니다.
int parse_sql(const char *sql, Command *command, char *error_message, int error_size) {
    // 원본 입력을 수정하지 않기 위해 사용할 작업용 복사본 버퍼입니다.
    char working_copy[1024];
    // 문자열 길이를 저장합니다.
    size_t length;

    // NULL 입력은 빈 SQL로 처리합니다.
    if (sql == NULL) {
        snprintf(error_message, error_size, "empty SQL input");
        return 0;
    }

    // 작업용 버퍼보다 긴 SQL은 안전하게 처리할 수 없으므로 거부합니다.
    if (strlen(sql) >= sizeof(working_copy)) {
        snprintf(error_message, error_size, "SQL is too long");
        return 0;
    }

    // 원본 SQL을 작업용 버퍼에 복사합니다.
    strcpy(working_copy, sql);
    // 앞뒤 공백을 제거합니다.
    trim_in_place(working_copy);
    // 정리된 문자열 길이를 구합니다.
    length = strlen(working_copy);

    // 공백만 있던 입력이면 빈 SQL입니다.
    if (length == 0) {
        snprintf(error_message, error_size, "empty SQL input");
        return 0;
    }

    // 이 파서는 세미콜론으로 끝나는 SQL만 허용합니다.
    if (working_copy[length - 1] != ';') {
        snprintf(error_message, error_size, "SQL must end with ';'");
        return 0;
    }

    // 마지막 세미콜론을 제거해 실제 문장 내용만 남깁니다.
    working_copy[length - 1] = '\0';
    // 세미콜론 앞 공백이 있었을 수 있으니 한 번 더 정리합니다.
    trim_in_place(working_copy);

    // 결과를 담을 Command 구조체를 초기화합니다.
    init_command(command);

    // INSERT 문이면 INSERT 파서로 보냅니다.
    if (starts_with_ignore_case(working_copy, "INSERT")) {
        return parse_insert(working_copy, command, error_message, error_size);
    }

    // SELECT 문이면 SELECT 파서로 보냅니다.
    if (starts_with_ignore_case(working_copy, "SELECT")) {
        return parse_select(working_copy, command, error_message, error_size);
    }

    // 현재 구현은 INSERT와 SELECT만 지원합니다.
    snprintf(error_message, error_size, "only INSERT and SELECT are supported");
    return 0;
}
