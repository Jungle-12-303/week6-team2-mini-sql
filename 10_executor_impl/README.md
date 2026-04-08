# 10_executor_impl

이 폴더의 목표는 **명령 종류에 따라 어느 함수를 실행할지 결정하는 코드**를 만드는 것입니다.

## 이 단계에서 만드는 파일

- `executor.c`

## 이 파일이 하는 일

`execute_command()`는 `Command.type`을 보고 분기합니다.

- `COMMAND_INSERT`면 `append_row_to_table()` 호출
- `COMMAND_SELECT`면 `print_table_rows()` 호출

즉, 실행기는 "직접 저장"보다 "어느 저장소 함수를 호출할지 결정"하는 역할입니다.
