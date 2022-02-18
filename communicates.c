#define _GNU_SOURCE
#define MESSAGE_MAX_LINE 4
#define MAX_LINE 100000
#define CLI 1
#define SERVER 0
#define COLOR_MESSAGE 1
#define COLOR_KEY 2

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <netdb.h>
#include <sys/socket.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/time.h>
#include <poll.h>
#include <ncurses.h>
#include <pthread.h>
#include <signal.h>

void *input(void *arg);
void *output(void *arg);
int message_put(int *poses, char *name, char *message);
int put_control_init(char *path);
int put_control_end();
int put_control_put(char *name, char *str);
int put_control_pull_change();
int put_control_pull(char *lines, int line_char_num);
int put_control_up();
int put_control_down();
long count_line(char *str);
int check_line(char *src);
void sigint_hand(int sig);
int line_check(FILE *fd, int *char_num, long *line_long);

int int2str(int num, char *str);
int str2int(char *str);

//送信前メッセージ出力の先頭
static int pos[2] = {30, 0};
static int pos_front[2] = {0,0};
static pthread_mutex_t mtx = PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t mtx_control = PTHREAD_MUTEX_INITIALIZER;

static struct control_dates{
	int pos_tail;
	FILE *out_fd;
	FILE *in_fd;
	int file_front;
	long line_long[MAX_LINE];	//fileの行ごとの文字数(\nも含む)
	int pos[2];
	int input_line_size;
} control_date;

void sigint_hand(int sig){
	put_control_end();

	exit(0);
}

int message(int sock, char *path){
	pthread_t in_t, out_t;
	struct sigaction act;
	void *res;
	int s;
	int x, y;

	act.sa_handler = sigint_hand;
	puts("チャットを開始します");
	sigaction(SIGINT, &act, NULL);
	put_control_init(path);
	getmaxyx(stdscr, y, x);
	y -= (MESSAGE_MAX_LINE + 1);
	pos[0] = y;
	move(y,0);
        s = pthread_create(&in_t, NULL, input, &sock);
        if(s != 0){
		fprintf(stderr, "pthread_create: err");
                exit(1);
        }
 
        s = pthread_create(&out_t, NULL, output, &sock);
        if(s != 0){
              	fprintf(stderr, "pthread_create2: err\n");
               	exit(1);
      	}
 
     	pthread_join(in_t, &res);
	close(sock);
	put_control_end();

	return 0;
}

void *output(void *arg){
	int sock = *(int *)(arg);
	char in[1024];
	
	for(;;){
		int num;
		char char_num[4];

		num = put_control_pull(in, sizeof in);
		int2str(num, char_num);
		write(sock, char_num, sizeof(int));
		write(sock, in, num);

		put_control_put("you", in);
		put_control_pull_change();
	}

	exit(0);
}

void *input(void *arg){
	int sock = *(int *)arg, nfds, point;
	char in[1024];
	struct pollfd fds[1];

	for(;;){
		char char_num[4];
		int num;

		fds[0].fd = sock;
		fds[0].events = POLLRDHUP;
		nfds = poll(fds, 1, 0);
		if(nfds == -1){
			fputs("poll: err\n", stderr);
			return NULL;
		}
		if(fds[0].revents & POLLRDHUP){
			puts("ピアソケットがクローズされました");
			return NULL;
		}

		read(sock, char_num, sizeof(int));
		num = str2int(char_num);
		read(sock, in, num);

		put_control_put("guest", in);
		put_control_pull_change();
	}
	exit(0);
}

//失敗したら-1を返す.who == 0でsever who == 1でcli
int put_control_init(char *path){
	FILE *in_fd, *out_fd;
	int char_num;
	
	printf("%sのファイrを作成します from put_control_init\n", path);
	out_fd = fopen(path, "a");
	in_fd = fopen(path, "r");

	if(in_fd < 0){
		fputs("in_fd open err\n", stderr);
		return -1;
	}
	if(out_fd < 0){
		fputs("out_fd open err\n", stderr);
		return -1;
	}

	initscr();
	start_color();
	keypad(stdscr, TRUE);
	nodelay(stdscr, TRUE);
	halfdelay(5);
	init_pair(COLOR_MESSAGE, COLOR_BLACK, 27);
	init_pair(COLOR_KEY, COLOR_BLACK, COLOR_WHITE);
	bkgd(COLOR_PAIR(COLOR_MESSAGE));
	attrset(COLOR_PAIR(COLOR_KEY));
	
	pthread_mutex_lock(&mtx_control);
	control_date.file_front = line_check(in_fd, &char_num, control_date.line_long);
	control_date.in_fd = in_fd;
	control_date.out_fd = out_fd;
	fseek(control_date.in_fd, char_num, SEEK_SET);
	control_date.pos_tail = 0;
	fseek(control_date.in_fd, char_num, SEEK_SET);

	getmaxyx(stdscr, control_date.input_line_size, char_num);

	pthread_mutex_unlock(&mtx_control);

	return 0;
}

//渡されたファイルディスクリプタを開放する
int put_control_end(){
	int i;

	pthread_mutex_lock(&mtx_control);
	fclose(control_date.in_fd);
	fclose(control_date.out_fd);
	pthread_mutex_unlock(&mtx_control);

	endwin();

	return 0;
}

//渡された文字列を端末に出力し、ファイルに保存する
//line_numは主力してもいい行数
int put_control_put(char *name, char *str){
	int pos_tail, pos_tail_p, line_tail, l, line_num, line_max_num;
	char buf[4096], *i;

	pthread_mutex_lock(&mtx_control);
	line_max_num = control_date.input_line_size - MESSAGE_MAX_LINE;
	pos_tail_p = control_date.pos_tail;
	line_num = check_line(str) + 2;
	line_tail = control_date.pos_tail;
	pos_tail = (line_tail + line_num);
	control_date.pos_tail = pos_tail;
	

	//行ごとの文字数を詰める
	strcpy(buf, name);
	strcat(buf, ":\n");
	strcat(buf, str);
	strcat(buf, "\n\n");

	fputs(buf, control_date.out_fd);
	i = buf;
	l = 0;
	while(l < line_num){
		char *p;
		p = i;
		if(!(i = strchr(p, '\n'))){
			control_date.line_long[l + line_tail + control_date.file_front] = count_line(p);
			break;
		}
		*i++ == '\0';
		control_date.line_long[l + line_tail + control_date.file_front] = count_line(p);
		l++;
	}

	pthread_mutex_unlock(&mtx_control);
	
	if(pos_tail < line_max_num){
		move(pos_tail_p, 0);
		printw("%s:\n%s\n", name, str);
	}
	
	while(pos_tail >= line_max_num){
		put_control_up(line_max_num);
		pos_tail--;
	}

	fseek(control_date.out_fd, -1, SEEK_END);

	getmaxyx(stdscr, control_date.input_line_size, line_num);
	
	return pos_tail;
}

int put_control_pull_change(){
	pthread_mutex_lock(&mtx_control);

	move(control_date.pos[0], control_date.pos[1]);

	pthread_mutex_unlock(&mtx_control);

	return 0;
}

//最大でmax_char_numデータを詰める
//読み取った文字数を返す
int put_control_pull(char *lines, int line_char_num){
	int i, input_front;

	pthread_mutex_lock(&mtx_control);
	input_front = control_date.input_line_size - MESSAGE_MAX_LINE;
	control_date.pos[0] = input_front;
	control_date.pos[1] = 0;
	pthread_mutex_unlock(&mtx_control);
	
	move(input_front, 0);
	for(i = 0;i<200;i++){
		int l;
		for(l=input_front;l < (input_front + MESSAGE_MAX_LINE);l++)
			mvaddch(l, i, ' ');
	}
	move(input_front, 0);

	for(i = 0;i < (line_char_num-1);i++){
		int in_char;

		do{
			in_char = getch();
		}while(in_char == -1);

		pthread_mutex_lock(&mtx_control);
		switch(in_char){
			case KEY_BACKSPACE:
			if(i == 0)
				i--;
			else{
				i -= 2;
				move(control_date.pos[0], --control_date.pos[1]);
				delch();
			}
			break;

			case KEY_UP:
			pthread_mutex_unlock(&mtx_control);
			put_control_up();
			put_control_pull_change();
			i--;
			break;

			case KEY_DOWN:
			pthread_mutex_unlock(&mtx_control);
			put_control_down();
			put_control_pull_change();
			i--;
			break;

			case '\n':
			control_date.pos[0]++;
			control_date.pos[1] = 0;
			move(control_date.pos[0], control_date.pos[1]);
			lines[i] = in_char;
			break;

			default:
			control_date.pos[1]++;
			lines[i] = in_char;
		}
		pthread_mutex_unlock(&mtx_control);

		if(in_char == '.')
			break;
	}
	lines[i+1] = '\0';

	return (i+2);
}	

//上にできなかったら-1を返す
//line_num : 出力してもいい行数
int put_control_up(){
	int i, line_long_num, line_max_num;
	long seek_num;
	char buf[4096];

	pthread_mutex_lock(&mtx_control);
	line_max_num = control_date.input_line_size - MESSAGE_MAX_LINE;

	//今いる行の行番号を計算する
	line_long_num = control_date.file_front;
	//移動する行の先頭の文字の絶対アドレスを計算
	seek_num = ftell(control_date.in_fd) + control_date.line_long[line_long_num];
	if((line_long_num + control_date.pos_tail) >= MAX_LINE || control_date.pos_tail <= 2){
		beep();

		pthread_mutex_unlock(&mtx_control);
		return -1;
	}
	fseek(control_date.in_fd, seek_num, SEEK_SET);
	move(0,0);
	for(i=0;i<line_max_num;i++){
		fgets(buf, sizeof buf, control_date.in_fd);
		if(buf == NULL) break;
		printw("%s", buf);
	}
	control_date.file_front++;
	control_date.pos_tail--;
	fseek(control_date.in_fd, seek_num, SEEK_SET);
	fseek(control_date.out_fd, -1, SEEK_END);

	move (0, 20);
	printw("%d", seek_num);
	pthread_mutex_unlock(&mtx_control);

	return 0;
}

//line_num 出力してもいい文字数 
int put_control_down(){
	int i, line_long_num, out_line_num, line_max_num;
	long seek_num;
	char buf[4096];

	pthread_mutex_lock(&mtx_control);
	line_max_num = control_date.input_line_size - MESSAGE_MAX_LINE;

	//一つ前の行の行番号を取得
	line_long_num = control_date.file_front - 1;
	seek_num = ftell(control_date.in_fd) - control_date.line_long[line_long_num];
	if(line_long_num  < 0){
		beep();
		pthread_mutex_unlock(&mtx_control);

		return -1;
	}
	fseek(control_date.in_fd, seek_num, SEEK_SET);
	if((control_date.pos_tail + 1) >= line_max_num)
		out_line_num = line_max_num;
	else
		out_line_num = control_date.pos_tail+1;
	move(0,0);
	for(i=0;i < out_line_num;i++){
		fgets(buf, sizeof buf, control_date.in_fd);
		printw("%s", buf);
	}
	control_date.file_front--;
	control_date.pos_tail++;
	fseek(control_date.in_fd, seek_num, SEEK_SET);

	move (0, 20);
	printw("%d", seek_num);
	pthread_mutex_unlock(&mtx_control);

	return 0;
}

//渡された文字列を１つ目の改行または、\0が出るまで文字数をカウントする。なお、文字数には改行を含める
long count_line(char *str){
	int count;

	for(count = 0;;count++){
		if(str[count] == '\n' || str[count] == '\0') break;
	}

	return (count+1);
}

//\0まで調べる
int check_line(char *src){
	int i, count = 0;

	for(i=0;;i++){
		if(src[i] == '\n') count++;
		if(src[i] == '\0') break;
	}

	return (count + 1);
}

int line_check(FILE *fd, int *char_num, long *line_long){
	int buf;
	int line_num = 0, lang_num = 0;
	long line_long_count = 0;

	do{
		buf = getc(fd);
		lang_num++;
		line_long_count++;
		if(buf == '\n' || buf == EOF || buf == '\0'){
			line_num++;
			line_long[line_num] = line_long_count;
			line_long_count = 0;
		}
		if(line_num >= MAX_LINE) break;
	}while(buf != EOF);

	*char_num = lang_num-1;

	return line_num;
}
