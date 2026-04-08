# 프로젝트를 어떻게 컴파일 하고 테스트할지 정의하는 파일 
CC = gcc
CFLAGS = -Wall -Wextra -Werror -std=c11 -pedantic


# APP_SRCS는 프로그램 실행 파일(sql_processor)을 만들 때 필요한 소스 파일 목록
APP_SRCS = main.c parser.c executor.c storage.c
TEST_SRCS = tests.c parser.c executor.c storage.c


# all, text, clean은 명령어 라고 알려주는 코드 
.PHONY: all test clean

# all → 전체 빌드용 명령
# test → 테스트 실행용 명령
# clean → 생성 파일 삭제용 명령




# make가 실행되면 기본적으로 all 목표를 처리해라라는 규칙
all: sql_processor

# APP_SRCS (main.c parser.c executor.c storage.c) 묶은거
# 한번에 컴파일 쌉가능하다~ 그런뜻

# $(CC) $(CFLAGS) ... -o sql_processor
# → 앱 실행 파일 만드는 실제 명령

sql_processor: $(APP_SRCS)
	$(CC) $(CFLAGS) $(APP_SRCS) -o sql_processor

tests: $(TEST_SRCS)
	$(CC) $(CFLAGS) $(TEST_SRCS) -o tests

test: tests
	./tests

clean:
	rm -f sql_processor tests
