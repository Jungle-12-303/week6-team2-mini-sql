# Rebuild 학습 체크리스트

- [x] 1. `rebuild/examples/insert_user.sql`을 다시 적고, 이 한 줄이 어떤 의미의 INSERT인지 설명한다.
- [x] 2. `rebuild/examples/select_users.sql`을 다시 적고, 출력이 `헤더 -> 데이터 행 -> Rows: N` 순서라는 점을 예측한다.
- [x] 3. `rebuild/data/users.csv`를 다시 만들고, header가 왜 schema 역할을 하는지 설명한다.
- [x] 4. `rebuild/common.h`를 다시 만들고, INSERT와 SELECT가 각각 어떤 필드를 채우는지 정리한다.
- [x] 5. `rebuild/parser.h`를 다시 만들고, 파서가 반환값과 `error` 버퍼를 같이 쓰는 이유를 설명한다.
- [x] 6. `rebuild/storage.h`를 다시 만들고, 저장 계층이 왜 두 함수로 나뉘는지 설명한다.
- [x] 7. `rebuild/executor.h`를 다시 만들고, executor의 책임이 판단과 연결이라는 점을 설명한다.
- [x] 8. `rebuild/parser.c`를 다섯 단계로 나눠 다시 만들고, parser 미니 테스트를 통과시킨다.
- [ ] 9. `rebuild/storage.c`를 네 단계로 나눠 다시 만들고, storage 미니 테스트를 통과시킨다.
- [ ] 10. `rebuild/executor.c`를 다시 만들고, `StatementType` 분기 흐름을 설명한다.
- [ ] 11. `rebuild/main.c`를 다시 만들고, `argc 검사 -> SQL 파일 읽기 -> parse -> execute -> 종료 코드` 흐름을 설명한다.
- [ ] 12. `rebuild/tests.c`를 마지막에 다시 만들고, 전체 테스트 구성을 설명한다.
- [ ] 13. `rebuild/Makefile`를 마지막에 다시 만들고, `APP_SRCS`와 `TEST_SRCS`를 나눈 이유를 설명한다.
- [ ] 14. `rebuild/README.md`, `rebuild/task.md`를 다시 쓰고, 입력 -> 파싱 -> 실행 -> 저장 흐름을 내 말로 문서화한다.
- [ ] 15. `rebuild/manual_check/insert_user.sql`, `rebuild/manual_check/select_users.sql`을 다시 만들고 최종 수동 검증에 사용한다.

## 현재 상태

- 현재 진행 중: 9번 `rebuild/storage.c` 1단계 (`set_error`, `build_table_path`, `strip_line_end`, `line_is_blank`)
- 진행 규칙: 파일 하나를 만든 뒤 역할 설명 -> 이해 확인 -> 다음 파일로 이동
