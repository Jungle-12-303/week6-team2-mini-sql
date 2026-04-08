#include "../common.h"
#include "../parser.h"

#include <stdio.h>
#include <string.h>

/*
채우는 목표:
1. 소문자 insert 파싱 성공
2. 세미콜론 누락 실패
3. 잘못된 SELECT 실패
4. quoted string 파싱 성공

사용 방법:
- TODO 주석 부분을 직접 채운다.
- 채운 뒤 아래처럼 컴파일해서 돌린다.

cd /Users/juhoseok/Desktop/sql_processor

gcc -Wall -Wextra -Werror -std=c11 -pedantic \
  rebuild/mini_tests/parser_mini_tests_todo.c \
  rebuild/parser.c -o rebuild/mini_tests/parser_mini_tests

./rebuild/mini_tests/parser_mini_tests

*/

static int tests_run = 0;
static int tests_failed = 0;

#define EXPECT_TRUE(condition, message)         \
    do {                                        \
        if (!(condition)) {                     \
            fprintf(stderr, "%s\n", (message)); \
            return 0;                           \
        }                                       \
    } while (0)

#define RUN_TEST(test_function)                     \
    do {                                           \
        tests_run++;                               \
        if (test_function()) {                     \
            printf("[PASS] %s\n", #test_function); \
        } else {                                   \
            printf("[FAIL] %s\n", #test_function); \
            tests_failed++;                        \
        }                                          \
    } while (0)

static int test_lowercase_insert(void) {
    Statement stmt;
    char error[MAX_ERROR_LENGTH];

    // int parse_sql(const char *sql, Statement *stmt, char *error, size_t error_size)
    /*
    TODO:
    - parse_sql(...)을 호출해서 소문자 insert가 성공하는지 검사
    - stmt.type == STATEMENT_INSERT 인지 검사
    - stmt.table_name == "users" 인지 검사
    - stmt.value_count == 3 인지 검사
    - stmt.values[1] == "Alice" 인지 검사
    */

    char sql[] = "insert INTO users VALUES (1, 'Alice', 20);";

    EXPECT_TRUE(parse_sql(sql, &stmt, error, sizeof(error)),
                "insert를 소문자로 넣었을때 테스트 ");
    EXPECT_TRUE(stmt.type == STATEMENT_INSERT, "insert타입인지 확인하는 테스트 ");
    EXPECT_TRUE(strcmp(stmt.table_name, "users") == 0, "테이블 이름이 users가 맞는지 확인하는 테스트 ");
    EXPECT_TRUE(stmt.value_count == 3, "3개의 값(1, Alice, 20)을 넣었는지 확인하는 테스트");
    EXPECT_TRUE(strcmp(stmt.values[1], "Alice") == 0,
                "세개의 값중 1번 인덱스가 Alice인지 확인하는 테스트");

    return 1;
}

static int test_missing_semicolon(void) {
    Statement stmt;
    char error[MAX_ERROR_LENGTH];

    /*
    TODO:
    - parse_sql(...)이 실패해야 한다
    - error 안에 "must end with ';'"가 들어있는지 검사한다
    */
    char sql[] = "insert INTO users VALUES (1, 'Alice', 20)"; // 세미콜론 없는 버전 
    // char sql[] = "insert INTO users VALUES (1, 'Alice', 20);";
    EXPECT_TRUE(!parse_sql(sql, &stmt, error, sizeof(error)), "끝에 세미롤론 안넣으면 false반환하는지 테스트"); //parse가 false면 true반환하게 
    EXPECT_TRUE(strstr(error, "must end with ';'") != NULL, "세미콜론 관련 에러 메시지가 있어야 함");



    return 1;
}

static int test_invalid_select(void) {
    Statement stmt;
    char error[MAX_ERROR_LENGTH];

    /*
    TODO:
    - 예: "SELECT users;" 같은 잘못된 SELECT를 넣는다
    - parse_sql(...)이 실패해야 한다
    - error 안에 "SELECT *" 관련 메시지가 들어있는지 검사한다
    */

    const char sql[] = "SELECT users;"; 
    EXPECT_TRUE(!parse_sql(sql, &stmt, error, sizeof(error)), "SELECT * FROM users라고 써야하는데 잘못쓴경우 ");
    EXPECT_TRUE(strstr(error, "SELECT *") != NULL, "error에 SELECT * 관련 메시지가 있어야 함"); // 관련 메시지가 null이 아니면 즉, 있으면 true


    return 1;
}

static int test_quoted_string_value(void) {
    Statement stmt;
    char error[MAX_ERROR_LENGTH];

    /*
    TODO:
    - "INSERT INTO users VALUES (1, 'Alice', 20);" 를 파싱한다
    - 성공해야 한다
    - stmt.values[1] 이 "'Alice'" 가 아니라 "Alice" 인지 검사한다
    */
    char sql[] = "insert INTO users VALUES (1, 'Alice', 20);";
    EXPECT_TRUE(parse_sql(sql, &stmt, error, sizeof(error)),"Alice insert했을때 작은 따옴표가 나오지 않는지 테스트 ");
    EXPECT_TRUE(strcmp(stmt.values[1], "Alice") == 0, "'Alice'를 읽었을 때 내부 값이 Alice인지 확인"); 
    //     strcmp(...) == 0 이 거짓이면 매크로 안에서 바로 return 0; 

    return 1;
}

int main(void) {
    RUN_TEST(test_lowercase_insert);
    RUN_TEST(test_missing_semicolon);
    RUN_TEST(test_invalid_select);
    RUN_TEST(test_quoted_string_value);

    printf("Tests run: %d\n", tests_run);
    printf("Tests failed: %d\n", tests_failed);

    return tests_failed == 0 ? 0 : 1;
}
