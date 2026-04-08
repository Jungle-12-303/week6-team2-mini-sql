#include "parser.h"   // parse_sql 선언과 Statement 타입을 쓰기 위해 포함한다.

#include <ctype.h>    // isspace, isalpha, isalnum, tolower 같은 문자 판별 함수들
#include <stdio.h>    // snprintf 사용
#include <string.h>   // strlen, memcpy, memmove, strchr, strrchr 사용

// 공통 에러 기록 함수:
// parser 곳곳에서 같은 방식으로 에러 문자열을 쓰기 위해 따로 뽑았다.
static void set_error(char *error, size_t error_size, const char *message) {
    if (error == NULL || error_size == 0) {  // 버퍼가 없으면 쓸 곳이 없으니 그냥 끝낸다.
        return;
    }

    snprintf(error, error_size, "%s", message);  // 버퍼 크기를 넘지 않게 에러 메시지를 복사한다.
}

// 현재 커서가 가리키는 위치에서 공백들을 건너뛴다.
// cursor 자체를 앞으로 움직여야 하므로 const char ** 를 받는다.
static void skip_spaces(const char **cursor) {
    while (**cursor != '\0' && isspace((unsigned char)**cursor)) {  // 현재 글자가 공백인 동안
        (*cursor)++;                                                // 바깥의 실제 커서를 한 칸 앞으로 민다.
    }
}

// 문자열 맨 앞/맨 뒤 공백을 제자리에서 제거한다.
// "   SELECT * FROM users;   " -> "SELECT * FROM users;"
static void trim_in_place(char *text) {
    char *start = text;                 // 앞쪽에서 첫 유효 문자 위치를 찾기 위한 포인터
    char *end;                          // 뒤쪽에서 마지막 유효 문자 위치를 찾기 위한 포인터

    while (*start != '\0' && isspace((unsigned char)*start)) {  // 앞 공백을 건너뛴다.
        start++;
    }

    if (start != text) {                              // 앞 공백이 실제로 있었다면
        memmove(text, start, strlen(start) + 1);      // 남은 문자열 전체를 앞으로 당긴다.
    }

    end = text + strlen(text);                        // 현재 문자열 끝('\0') 위치로 이동한다.
    while (end > text && isspace((unsigned char)*(end - 1))) {  // 뒤 공백이 있으면
        end--;                                        // 끝 포인터를 앞으로 당긴다.
    }

    *end = '\0';                                      // 새 문자열 끝을 찍어서 뒤 공백을 잘라낸다.
}

// 현재 커서 위치에서 keyword가 정확히 시작하는지 확인하고,
// 맞으면 그 키워드 뒤로 커서를 이동시킨다.
static int consume_keyword(const char **cursor, const char *keyword) {
    const char *text = *cursor;   // 현재 검사 시작 위치를 읽기 쉽게 별도 이름으로 둔다.
    size_t index = 0;             // keyword 몇 글자째를 보고 있는지 세는 인덱스

    while (keyword[index] != '\0') {  // keyword 끝까지 한 글자씩 비교한다.
        if (text[index] == '\0') {    // 입력이 더 짧으면 keyword를 완전히 읽을 수 없다.
            return 0;
        }

        if (tolower((unsigned char)text[index]) !=
            tolower((unsigned char)keyword[index])) {  // 대소문자 무시 비교
            return 0;                                  // 한 글자라도 다르면 실패
        }

        index++;                                       // 다음 글자로 이동
    }

    // INSERTX 같은 경우를 막기 위한 검사:
    // keyword 뒤가 공백도 아니고 문자열 끝도 아니면 진짜 keyword로 인정하지 않는다.
    if (text[index] != '\0' && !isspace((unsigned char)text[index])) {
        return 0;
    }

    *cursor = text + index;  // keyword를 성공적으로 읽었으니 커서를 그 뒤로 옮긴다.
    return 1;                // 성공
}

// 문자열이 keyword로 시작하는지만 본다.
// cursor를 옮기지 않는 "미리 보기" 용도다.
static int starts_with_keyword(const char *text, const char *keyword) {
    size_t index = 0;  // keyword를 앞에서부터 비교하기 위한 인덱스

    while (keyword[index] != '\0') {  // keyword 끝까지 비교
        if (text[index] == '\0') {    // 입력이 먼저 끝나면 실패
            return 0;
        }

        if (tolower((unsigned char)text[index]) !=
            tolower((unsigned char)keyword[index])) {  // 대소문자 무시 비교
            return 0;
        }

        index++;                                       // 다음 글자로 이동
    }

    return 1;  // keyword 길이만큼 모두 일치하면 성공
}

// 테이블 이름 같은 식별자를 읽는다.
// 규칙:
// - 첫 글자: 알파벳 또는 _
// - 나머지: 알파벳/숫자/_
static int parse_identifier(const char **cursor,
                            char *destination,
                            size_t destination_size,
                            char *error,
                            size_t error_size) {
    const char *text = *cursor;  // 현재 읽기 시작 위치
    size_t length = 0;           // destination에 몇 글자 썼는지 기록

    if (!(isalpha((unsigned char)*text) || *text == '_')) {  // 첫 글자 규칙 검사
        set_error(error, error_size, "Expected table name");
        return 0;
    }

    while (isalnum((unsigned char)*text) || *text == '_') {  // 식별자 규칙에 맞는 동안 계속 읽는다.
        if (length + 1 >= destination_size) {                // 마지막 '\0' 자리까지 남아 있어야 한다.
            set_error(error, error_size, "Table name is too long");
            return 0;
        }

        destination[length++] = *text;  // 현재 글자를 복사하고
        text++;                         // 입력 커서를 한 칸 앞으로 이동한다.
    }

    destination[length] = '\0';  // C 문자열 끝 표시
    *cursor = text;              // 바깥 커서도 지금 읽은 위치 다음으로 옮긴다.
    return 1;
}

// 작은따옴표가 없는 값 하나를 읽는다.
// 예: 1, 20, Alice
// 종료 기준은 쉼표(,) 또는 닫는 괄호())다.
static int parse_unquoted_value(const char **cursor,
                                char *destination,
                                size_t destination_size,
                                char *error,
                                size_t error_size) {
    const char *start = *cursor;  // 값 시작 후보 위치
    const char *end = start;      // 값 끝 후보 위치
    size_t length;                // 최종 값 길이
    size_t index;                 // 복사용 인덱스

    while (*end != '\0' && *end != ',' && *end != ')') {  // 값 끝을 찾을 때까지 전진
        end++;
    }

    while (end > start && isspace((unsigned char)*(end - 1))) {  // 뒤쪽 공백 제거
        end--;
    }

    while (start < end && isspace((unsigned char)*start)) {      // 앞쪽 공백 제거
        start++;
    }

    if (start == end) {  // 실제 값이 하나도 없으면 빈 값이므로 실패
        set_error(error, error_size, "Empty value is not allowed");
        return 0;
    }

    length = (size_t)(end - start);          // 잘라낸 실제 값 길이 계산
    if (length >= destination_size) {        // '\0'까지 저장할 공간이 없으면 실패
        set_error(error, error_size, "Value is too long");
        return 0;
    }

    for (index = 0; index < length; index++) {             // 값 내용을 한 글자씩 복사
        if (isspace((unsigned char)start[index])) {        // 따옴표 없는 값 중간 공백은 허용하지 않는다.
            set_error(error, error_size,
                      "Unquoted values cannot contain spaces");
            return 0;
        }

        destination[index] = start[index];
    }

    destination[length] = '\0';  // 문자열 종료
    *cursor = end;               // 값 읽기가 끝난 지점으로 커서를 이동
    return 1;
}

// 작은따옴표로 감싼 문자열 값 하나를 읽는다.
// 예: 'Alice' -> 저장 결과는 Alice
static int parse_quoted_value(const char **cursor,
                              char *destination,
                              size_t destination_size,
                              char *error,
                              size_t error_size) {
    const char *text = *cursor;  // 현재 값 시작 위치. 처음 글자는 ' 이어야 한다.
    size_t length = 0;           // destination에 저장한 길이

    text++;  // 맨 앞 작은따옴표(')는 문법 기호이므로 건너뛴다.

    while (*text != '\0' && *text != '\'') {  // 닫는 작은따옴표를 만날 때까지 내용 복사
        if (length + 1 >= destination_size) { // 마지막 '\0' 자리까지 포함해 크기 검사
            set_error(error, error_size, "Value is too long");
            return 0;
        }

        destination[length++] = *text;  // 작은따옴표 안쪽 실제 문자만 저장
        text++;
    }

    if (*text != '\'') {  // 문자열 끝까지 가도 닫는 작은따옴표가 없으면 실패
        set_error(error, error_size, "Unterminated string value");
        return 0;
    }

    destination[length] = '\0';  // 저장된 문자열 종료
    text++;                      // 닫는 작은따옴표도 소비했으니 다음 위치로 이동
    *cursor = text;              // 바깥 커서 반영
    return 1;
}

// VALUES (...) 전체를 읽는다.
// 예: (1, 'Alice', 20)
static int parse_values_list(const char **cursor,
                             Statement *stmt,
                             char *error,
                             size_t error_size) {
    const char *text = *cursor;  // 현재 VALUES 뒤 위치에서 읽기 시작

    if (*text != '(') {          // 값 목록은 반드시 여는 괄호로 시작해야 한다.
        set_error(error, error_size, "Expected '(' after VALUES");
        return 0;
    }

    text++;                      // '('를 소비하고 첫 값 쪽으로 이동
    stmt->value_count = 0;       // 새 INSERT 문장 파싱 시작이므로 값 개수 초기화

    for (;;) {                   // 쉼표/닫는 괄호를 만날 때까지 값들을 반복해서 읽는다.
        skip_spaces(&text);      // 값 앞 공백은 무시

        if (*text == ')') {      // VALUES()처럼 아무 값도 없는 경우
            set_error(error, error_size, "VALUES list cannot be empty");
            return 0;
        }

        if (stmt->value_count >= MAX_VALUES) {  // 고정 배열 한도를 넘으면 실패
            set_error(error, error_size, "Too many values");
            return 0;
        }

        if (*text == '\'') {  // 작은따옴표로 시작하면 quoted value
            if (!parse_quoted_value(&text,
                                    stmt->values[stmt->value_count],
                                    MAX_VALUE_LENGTH,
                                    error,
                                    error_size)) {
                return 0;
            }
        } else {             // 그 외에는 unquoted value
            if (!parse_unquoted_value(&text,
                                      stmt->values[stmt->value_count],
                                      MAX_VALUE_LENGTH,
                                      error,
                                      error_size)) {
                return 0;
            }
        }

        stmt->value_count++;   // 값 하나를 성공적으로 읽었으니 개수를 1 증가
        skip_spaces(&text);    // 값 뒤 공백 제거

        if (*text == ',') {    // 쉼표면 다음 값이 더 있다는 뜻
            text++;
            continue;
        }

        if (*text == ')') {    // 닫는 괄호면 값 목록 끝
            text++;
            break;
        }

        set_error(error, error_size, "Expected ',' or ')' in VALUES list");
        return 0;
    }

    skip_spaces(&text);        // 괄호 뒤 공백 제거
    if (*text != '\0') {       // 값 목록 뒤에 다른 이상한 글자가 남아 있으면 실패
        set_error(error, error_size, "Unexpected text after VALUES list");
        return 0;
    }

    *cursor = text;            // 최종 읽은 위치 반영
    return 1;
}

// INSERT 문장 전체를 읽는다.
// 형식:
// INSERT INTO table_name VALUES (...)
static int parse_insert(const char *sql,
                        Statement *stmt,
                        char *error,
                        size_t error_size) {
    const char *cursor = sql;                // INSERT 문장 맨 앞에서 시작

    stmt->type = STATEMENT_INSERT;           // 이 문장은 INSERT 타입이라고 기록
    stmt->value_count = 0;                   // 값 개수는 새로 세기 시작

    if (!consume_keyword(&cursor, "INSERT")) {  // 첫 키워드는 INSERT여야 한다.
        set_error(error, error_size, "Expected INSERT");
        return 0;
    }

    skip_spaces(&cursor);                       // 키워드 뒤 공백 건너뛰기
    if (!consume_keyword(&cursor, "INTO")) {   // 다음은 INTO여야 한다.
        set_error(error, error_size, "Expected INTO after INSERT");
        return 0;
    }

    skip_spaces(&cursor);                      // INTO 뒤 공백 건너뛰기
    if (!parse_identifier(&cursor,
                          stmt->table_name,
                          sizeof(stmt->table_name),
                          error,
                          error_size)) {       // 테이블 이름을 읽어 stmt에 저장
        return 0;
    }

    skip_spaces(&cursor);                      // 테이블 이름 뒤 공백 건너뛰기
    if (!consume_keyword(&cursor, "VALUES")) { // 다음은 VALUES여야 한다.
        set_error(error, error_size, "Expected VALUES after table name");
        return 0;
    }

    skip_spaces(&cursor);                      // VALUES 뒤 공백 건너뛰기
    return parse_values_list(&cursor, stmt, error, error_size);  // 실제 값 목록 파싱으로 위임
}

// SELECT 문장 전체를 읽는다.
// 현재 지원 형식은:
// SELECT * FROM table_name
static int parse_select(const char *sql,
                        Statement *stmt,
                        char *error,
                        size_t error_size) {
    const char *cursor = sql;                // SELECT 문장 맨 앞에서 시작

    stmt->type = STATEMENT_SELECT;           // 이 문장은 SELECT 타입이라고 기록
    stmt->value_count = 0;                   // SELECT는 값 목록을 쓰지 않으므로 0으로 둔다.

    if (!consume_keyword(&cursor, "SELECT")) {  // 첫 키워드는 SELECT여야 한다.
        set_error(error, error_size, "Expected SELECT");
        return 0;
    }

    skip_spaces(&cursor);                      // SELECT 뒤 공백 제거
    if (*cursor != '*') {                     // 현재 MVP는 SELECT * 만 지원
        set_error(error, error_size, "Only SELECT * is supported");
        return 0;
    }

    cursor++;                                 // '*'를 소비
    skip_spaces(&cursor);                     // '*' 뒤 공백 제거

    if (!consume_keyword(&cursor, "FROM")) {  // 다음은 FROM이어야 한다.
        set_error(error, error_size, "Expected FROM after SELECT *");
        return 0;
    }

    skip_spaces(&cursor);                     // FROM 뒤 공백 제거
    if (!parse_identifier(&cursor,
                          stmt->table_name,
                          sizeof(stmt->table_name),
                          error,
                          error_size)) {      // 테이블 이름 읽기
        return 0;
    }

    skip_spaces(&cursor);                     // 테이블 이름 뒤 공백 제거
    if (*cursor != '\0') {                    // 문장 끝까지 다 읽었어야 정상
        set_error(error, error_size, "Unexpected text after table name");
        return 0;
    }

    return 1;
}

// parser의 공개 진입점:
// 입력 전체를 정리하고, 문장 종류를 판별한 뒤,
// 알맞은 하위 파서(parse_insert / parse_select)로 넘긴다.
int parse_sql(const char *sql, Statement *stmt, char *error, size_t error_size) {
    char buffer[MAX_SQL_LENGTH];            // 원본 입력을 안전하게 다듬기 위한 작업용 버퍼
    char *semicolon;                        // 첫 번째 세미콜론 위치
    char *last_semicolon;                   // 마지막 세미콜론 위치
    size_t length;                          // 입력 길이

    if (sql == NULL || stmt == NULL) {     // 필수 입력이 없으면 내부 사용 오류
        set_error(error, error_size, "Internal parser error");
        return 0;
    }

    length = strlen(sql);                  // 입력 길이 확인
    if (length >= sizeof(buffer)) {        // 버퍼보다 길면 복사할 수 없으니 실패
        set_error(error, error_size, "SQL input is too long");
        return 0;
    }

    memcpy(buffer, sql, length + 1);       // 원본 sql은 const 이므로 작업용 buffer로 복사
    trim_in_place(buffer);                 // 앞뒤 공백 제거

    if (buffer[0] == '\0') {               // trim 후 비어 있으면 빈 입력
        set_error(error, error_size, "SQL input is empty");
        return 0;
    }

    semicolon = strchr(buffer, ';');       // 첫 번째 세미콜론 찾기
    if (semicolon == NULL) {               // 세미콜론이 없으면 문장 종료 규칙 위반
        set_error(error, error_size, "SQL statement must end with ';'");
        return 0;
    }

    last_semicolon = strrchr(buffer, ';'); // 마지막 세미콜론 찾기
    if (semicolon != last_semicolon) {     // 둘이 다르면 세미콜론이 여러 개 있다는 뜻
        set_error(error, error_size, "Only one SQL statement is supported");
        return 0;
    }

    if (semicolon[1] != '\0') {            // 세미콜론 뒤에 다른 글자가 남아 있으면 실패
        set_error(error, error_size, "Unexpected text after ';'");
        return 0;
    }

    *semicolon = '\0';                     // 실제 파싱은 세미콜론 없이 진행할 것이므로 제거
    trim_in_place(buffer);                 // 세미콜론 제거 후 다시 앞뒤 공백 정리

    if (starts_with_keyword(buffer, "INSERT")) {  // INSERT 문장인지 먼저 확인
        return parse_insert(buffer, stmt, error, error_size);
    }

    if (starts_with_keyword(buffer, "SELECT")) {  // 아니면 SELECT 문장인지 확인
        return parse_select(buffer, stmt, error, error_size);
    }

    set_error(error, error_size, "Only INSERT and SELECT are supported");  // 현재 범위 밖 문장
    return 0;
}
