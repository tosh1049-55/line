cli.out sever.out: bytes.c cli.c server.c communicates.c
	gcc -pthread server.c bytes.c -o server.out
	gcc -pthread bytes.c communicates.c -lncurses cli.c -o cli.out

clean:
	rm *.out
	sudo rm -i /usr/message/*

cli_run:
	./cli.out localhost 100

sever_run:
	sudo ./server.out 100
