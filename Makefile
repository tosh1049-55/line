cli.out sever.out: cli.c server.c communicates.c
	gcc -pthread bytes.c communicates.c -lncurses cli.c -o cli.out
	gcc -pthread bytes.c server.c -o server.out

clean:
	rm *.out
	rm message_server
	rm message_cli

cli_run:
	./cli.out localhost 100

sever_run:
	sudo ./server.out 100
