# Rebuild 학습 체크리스트

## 진행 규칙

- [ ] 한 파일을 만든 뒤 그 파일의 역할을 내 말로 설명한다.
- [ ] 설명이 되면 다음 파일 또는 다음 폴더로 이동한다.
- [ ] 각 폴더에는 반드시 `README.md`를 둔다.
- [ ] 이해가 안 되는 파일은 다시 더 작은 단계로 나눈다.

## 1단계. SQL 입력부터 익숙해지기

- [x] `01_insert_sql/insert_user.sql`을 직접 다시 쓴다.
- [x] `INSERT`, `INTO`, `VALUES`, 세미콜론(`;`)의 의미를 설명한다.
- [x] 왜 첫 단계에서 코드가 아니라 SQL 파일부터 시작하는지 설명한다.
- [x] `01_insert_sql/README.md`를 읽고 한 줄 SQL의 구조를 이해한다.

## 2단계. SELECT 예제 만들기

- [x] `02_select_sql/select_users.sql`을 만든다.
- [x] `SELECT * FROM users;`가 어떤 요청인지 설명한다.
- [x] INSERT와 SELECT의 차이를 설명한다.

## 3단계. 저장 파일 이해하기

- [x] `03_data/users.csv`를 만든다.
- [x] CSV 한 줄이 row라는 점을 설명한다.
- [x] 왜 지금은 DB 대신 CSV를 쓰는지 설명한다.

## 4단계. 공통 구조 만들기

- [x] `04_common/common.h`를 만든다.
- [x] `CommandType`, `Command`가 왜 필요한지 설명한다.
- [x] 파서와 실행기가 왜 같은 구조체를 공유해야 하는지 설명한다.

## 5단계. 파서 헤더 만들기

- [x] `05_parser/parser.h`를 만든다.
- [x] `parse_sql`, `init_command`, `free_command`의 역할을 설명한다.

## 6단계. 저장소 헤더 만들기

- [x] `06_storage/storage.h`를 만든다.
- [x] 저장 계층이 무엇을 책임지는지 설명한다.

## 7단계. 실행기 헤더 만들기

- [x] `07_executor/executor.h`를 만든다.
- [x] 실행기가 왜 분기 담당인지 설명한다.

## 8단계. 파서 구현 만들기

- [x] `08_parser_impl/parser.c`를 만든다.
- [x] SQL 문자열을 구조체로 바꾸는 과정을 설명한다.

## 9단계. 저장소 구현 만들기

- [x] `09_storage_impl/storage.c`를 만든다.
- [x] 파일 읽기/쓰기 흐름을 설명한다.

## 10단계. 실행기 구현 만들기

- [x] `10_executor_impl/executor.c`를 만든다.
- [x] 명령 종류에 따라 어느 함수를 부르는지 설명한다.

## 11단계. 메인 연결하기

- [x] `11_main/main.c`를 만든다.
- [x] `읽기 -> 파싱 -> 실행 -> 종료` 흐름을 설명한다.

## 12단계. 테스트와 빌드

- [x] `12_tests/test.sh`를 만든다.
- [x] `Makefile`을 만든다.
- [x] 왜 마지막에 테스트를 붙이는지 설명한다.

## 현재 상태

- [x] 전체 학습용 파일 생성 완료
- [ ] 다음 목표: 각 `.c` 파일을 한 줄씩 읽으며 직접 설명해 보기
