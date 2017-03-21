SRCS1 := mini_shell.c
TRGT1 := mini_shell

${TRGT1} : ${SRCS1}
	gcc $^ -o $@

clean :
	rm -f ${TRGT1}
