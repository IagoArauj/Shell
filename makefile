all: prog

prog:
	gcc src/main.c -o prog -lreadline -lm

clean:
	rm prog