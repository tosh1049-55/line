#define _GNU_SOURCE

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <netdb.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <unistd.h>
#include <sys/types.h>
#include <pthread.h>
#include <poll.h>

#define NAME_MAX 128

struct service_data{
	int sock;
	FILE *user_fd;
	FILE *pass_fd;
};

struct threads{
	char receiver[NAME_MAX];
	char transmitter[NAME_MAX];
	int sock;
	int receiver_sock;
	int check[2];
	struct threads *next;
};

static struct threads *threads_front = NULL;

//int型をsizeof(int)個のchar型として記録する
extern int int2str(int num, char *str);
//sizeof(int)個のchar型をintにして返す
extern int str2int(char *str);

int listen_socket(char *port);
void *service(void *arg);
//すでに自分と通信している人がいたとき
int connect_user(struct threads *receiver_section, int sock);
//ファイルに保存しておくとき
int save_user(struct threads *section);

struct threads *threads_back(struct threads *data);
void server_exit(int sock, int receiver_sock, FILE *fd, char *transmitter, char *receiver);
//transmitterからreceiverへのファイルパスをpathに入れる
int path_create(char *receiver, char *transmitter, char *path);
//userとpassを認証する。もしユーザーが登録されていなければ、登録しておく。パスワードがあっていたら0,パスワードが間違っていたら-1を返す
int user_check(FILE *user_fd, FILE *pass_fd, char *user, char *pass);
//fdの指すファイルから、strと等しくなる行を調べる。見つかったら行の先頭から何行目かを返す。見つからなかったら、-1を返す。
int select_line(FILE *, char *);
//fdガサスファイルのline_num行目をlineに詰める。ただし、改行は含めない。成功したら、0失敗したら-1を返す。
int take_line(FILE *fd, int line_num, char *line);
/*threadsを新しく作り、receiver, transmitter, sockを詰めて、thread_frontに設定されている先頭を新しく作ったものにする。
また、もとから設定されていたものはnextに詰める。成功したら構造体のポインタを,　失敗したらNULLを返す*/
struct threads *threads_put(char *receiver, char *transmitter, int sock);
//receiver, transmitterを満たすthreads型のデータをthreads_frontから探っっていく
struct threads *threads_select(char *receiver, char *transmitter);
//引数のソケットのピアソケットが閉じていないかチェック、成功したら0,　しっぱいしたら-1
int sock_check(int sock);
//ファイルの内容をsockに代入する,成功したら0, 失敗したら-1
int file2sock(FILE *fd, int sock);
//userとpassをファイルuser_fd, pass_fdに設定する
int user_list(FILE *user_fd, FILE *pass_fd, char *user, char *pass);

int main(int argc, char *argv[]){
	int i, server, sock;
	struct sockaddr addr;
	struct service_data data;
	socklen_t addr_len = sizeof(struct sockaddr);
	char buf[1024], in[2048];
	pid_t pi;
	FILE *user_fd, *pass_fd;
	
	if(argc < 2){
		fprintf(stderr, "%s port\n", argv[0]);
		exit(1);
	}

	user_fd = fopen("/etc/users", "r+");
	pass_fd = fopen("/etc/pass", "r+");
	
	data.user_fd = user_fd;
	data.pass_fd = pass_fd;

	server = listen_socket(argv[1]);
	for(i = 0;;i++){
		pthread_t t1;
		void *res;
		int s;

		sock = accept(server, &addr, &addr_len);

		if(sock == -1) {
			fprintf(stderr, "accept failed\n");
			exit(0);
		}

		data.sock = sock;
		pthread_create(&t1, NULL, service, (void *)(&data));
	}
	close(sock);

	return 0;
}

void server_exit(int sock, int receiver_sock, FILE *fd, char *transmitter, char *receiver){
	struct threads *data, *p;

	close(sock);
	close(receiver_sock);
	fclose(fd);

	if(data = threads_select(transmitter, receiver)){
		printf("receive:%s transmitter:%s from server_exit\n", transmitter, receiver);
		p = threads_back(data);
		if(p == data)
			threads_front = NULL;
		else if(p)
			p->next = data->next;
		free(data);
		return;
	}
	if(data = threads_select(receiver, transmitter)){
		printf("receive:%s transmitter:%s from server_exit\n", receiver, transmitter);
		p = threads_back(data);
		if(p == data)
			threads_front = NULL;
		else if(p)
			p->next = data->next;
		free(data);
		return;
	}
}

//When data is front, return is data;
//When data was looked, return is back_data;
//When data wasn't, return is NULL;
struct threads *threads_back(struct threads *data){
	int i;
	struct threads *p, *r = data;

	for(p = threads_front;p != data;p = p->next){
		if(p == NULL){
			r = NULL;
			break;
		}
			
		r = p;
	}
	return r;
}


void *service(void *arg){
	int sock;
	FILE *user_fd, *pass_fd;
	char pass[NAME_MAX], receiver[NAME_MAX], transmitter[NAME_MAX], buf[NAME_MAX*2 + 3], *p, *r;
	struct service_data *data = (struct service_data *)arg;
	struct threads *partner;

	sock = data->sock;
	user_fd = data->user_fd;
	pass_fd = data->pass_fd;

	puts("ユーザー認証を開始します");
	while(1){	//認証。ユーザーが見つからなかったら作る
		char char_num[4];
		int num;
		
		read(sock, char_num, sizeof(int));
		num = str2int(char_num);
		read(sock, buf, num);
		if((p = strchr(buf, '\n')) == NULL){
			fputs("cli stail err\n", stderr);
			exit(1);
		}
		*p++ = '\0';
		strcpy(transmitter, buf);
		strcpy(pass, p);
		if(user_check(user_fd, pass_fd, transmitter, pass) < 0)
			write(sock, "NO", 3);
		else{
			write(sock, "OK", 3);
			break;
		}

		if(sock_check(sock) == 1){
			fputs("sock is close\n", stderr);
			pthread_exit(NULL);
		}
	}
	puts("ユーザー認証に成功しました。通信相手を探します。通信相手は");

	while(1){	//通信相手を探す。いなかったらOK,いたらNOを送る
		char char_num[4];
		int num;
		
		read(sock, char_num, sizeof(int));
		num = str2int(char_num);
		read(sock, buf, num);
		strcpy(receiver, buf);
		puts(receiver);
		if(select_line(user_fd, receiver) < 0)
			write(sock, "NO", 3);
		else{
			write(sock, "OK", 3);
			break;
		}

		if(sock_check(sock) == 1){
			fputs("sock is closed\n", stderr);
			pthread_exit(NULL);
		}
	}

	puts("通信がすでにされているかチェックします");
	//すでに相手が自分と通信しているかチェックする
	if((partner = threads_select(transmitter, receiver))){
		puts("ユーザをつなげます");
		connect_user(partner, sock);
	}else{	//いなかった場合
		struct threads *data;

		if((data = threads_put(receiver, transmitter, sock)) == NULL){
			fprintf(stderr, "put_threads: err\n");
			exit(1);
		}
		puts("ファイルに保存します");
		save_user(data);
	}
}

int connect_user(struct threads *receiver_section, int sock){
	int receiver_sock;
	char receiver[NAME_MAX], transmitter[NAME_MAX], receiver_path[NAME_MAX*2 + 12];
	FILE *receiver_fd;

	receiver_sock = receiver_section->sock;
	strcpy(transmitter, receiver_section->receiver);
	strcpy(receiver, receiver_section->transmitter);

	receiver_section->receiver_sock = sock;
	receiver_section->check[1] = 1;

	while(receiver_section->check[0] == 0);

	path_create(transmitter, receiver, receiver_path);
	receiver_fd = fopen(receiver_path, "r");
	file2sock(receiver_fd, sock);
	fclose(receiver_fd);
	unlink(receiver_path);

	while(1){
		int num;
		char char_num[4], in[2048];

		if(sock_check(sock) == 1 || sock_check(receiver_sock) == 1){	//sockが閉じていれば終了
			server_exit(sock, receiver_sock, receiver_fd, transmitter, receiver);
			pthread_exit(NULL);
		}
		//相手のメッセージをこちらに書き込む
		read(receiver_sock, char_num, sizeof(int));
		num = str2int(char_num);
		read(receiver_sock, in, num);
		printf("from %s to %s: %s\n", receiver, transmitter, in);
		write(sock, char_num, sizeof(4));
		write(sock, in, num);
	}

	return 0;
}

int save_user(struct threads *section){
	int sock, receiver_sock;
	char char_num[4], in[2048], transmitter[NAME_MAX], receiver[NAME_MAX], transmitter2receiver[NAME_MAX*2 + 12], receiver2transmitter[NAME_MAX*2 + 12];
	FILE *to_transmitter, *to_receiver;
	struct stat buf;

	sock = section->sock;
	strcpy(transmitter, section->transmitter);
	strcpy(receiver, section->receiver);

	printf("相手は%s. from save_user\n", receiver);
	printf("ユーザは%s. from save_user\n", transmitter);

	path_create(receiver, transmitter, transmitter2receiver);
	path_create(transmitter, receiver, receiver2transmitter);

	printf("相手へのメッセージを記録しておくファイル名は%sです\n", transmitter2receiver);
	printf("相手からのメセージが記録してあるファイル名は%sです\n", receiver2transmitter);

	puts("チャット保存用のファイルを開きます");
	if((to_receiver = fopen(transmitter2receiver, "a+")) < 0)
		return -1;

	if(stat(receiver2transmitter, &buf) != -1){
		if((to_transmitter = fopen(receiver2transmitter, "r")) < 0)
			return -1;
		puts("これから、溜まっていたメッセージを送信します");
		file2sock(to_transmitter, sock);

		fclose(to_transmitter);
		unlink(receiver2transmitter);
	}
	
	while(1){
		int char_num;
		char nums[4], in[2048];

		if(section->check[1] == 1){
			receiver_sock = section->receiver_sock;	//相手が自分のsockを書き込んでおいてくれる
			fclose(to_receiver);
			section->check[0] = 1;
			break;
		}
	
		read(sock, nums, sizeof(int));
		char_num = str2int(nums);
		read(sock, in, char_num);
		if(sock_check(sock) == 1){	//sockが閉じていれば終了
			puts("sock is closed");
			server_exit(sock, receiver_sock, to_receiver, transmitter, receiver);
			pthread_exit(NULL);
		}
		printf("%s :を書き込みます\n", in);
		fprintf(to_receiver, "%s\n\n", in);
	}

	puts("これからつなげます from save_file");
	while(1){
		int num;
		char char_num[4], in[2048];

		if(sock_check(sock) == 1 || sock_check(receiver_sock)){	//sockが閉じていれば終了
			fputs("sock is closed\n", stderr);		
			server_exit(sock, receiver_sock, to_receiver, transmitter, receiver);
			pthread_exit(NULL);
		}
		//receiver_sockのメッセージをsockに書き込む
		read(receiver_sock, char_num, sizeof(int));
		num = str2int(char_num);
		read(receiver_sock, in, num);
		printf("from %s to %s: %s\n", receiver, transmitter, in);
		write(sock, char_num, sizeof(4));
		write(sock, in, num);
	}

	return 0;
}

int path_create(char *receiver, char *transmitter, char *path){
	printf("受け取る側の名前は%s from path_create\n", receiver);
	printf("送る側の名前は%s from path_create\n", transmitter);
	strcpy(path, "/usr/users/");
	strcat(path, receiver);
	strcat(path, "/");
	strcat(path, transmitter);

	return 0;
}

	
//userが登録されていなければ登録しておく
int user_check(FILE *user_fd, FILE *pass_fd, char *user, char *pass){
	int user_line;
	char buf[NAME_MAX + 1];

	puts("認証に入ります");
	user_line = select_line(user_fd, user);
	if(user_line < 0){
		puts("ユーザを作成します");
		user_list(user_fd, pass_fd, user, pass);
	}else{
		printf("パスワードの認証をします.パスワードは%s,調べる位置は%d\n", pass, user_line);
		take_line(pass_fd, user_line, buf);
		if(strcmp(pass, buf) != 0)
			return -1;
		puts("パスワードの認証に成功しました");
	}

	return 0;
}


//hostでipアドレスまたはドメインを指定 serviceでポート番号またはサービス名を指定
int listen_socket(char *port){
	int sock, err;
	struct addrinfo hints, *res, *ai;

	memset(&hints, 0, sizeof(struct addrinfo));
	hints.ai_family = AF_UNSPEC;
	hints.ai_socktype = SOCK_STREAM;
	hints.ai_flags = AI_PASSIVE | AI_NUMERICSERV;
	if((err = getaddrinfo(NULL, port, &hints, &res)) != 0){
		fprintf(stderr, "getaddrinfo\n");
		exit(1);
	}
	for(ai = res;ai;ai = ai->ai_next){
		sock = socket(ai->ai_family, ai->ai_socktype, ai->ai_protocol);
		if(sock < 0) {
			printf("socketの問題です\n");
			continue;
		}
		if(bind(sock, ai->ai_addr, ai->ai_addrlen) < 0){
			close(sock);
			printf("%s,%d\n", ai->ai_addr->sa_data, ai->ai_addrlen);
			printf("bindの問題です\n");
			continue;
		}
		if(listen(sock, 3) < 0){
			close(sock);
			printf("listenの問題です\n");
			continue;
		}
		freeaddrinfo(res);
		return sock;
	}
	fprintf(stderr, "failed to listen socket\n");
	freeaddrinfo(res);
	exit(1);
}

int select_line(FILE *fd, char *str){
	int count;
	char buf[NAME_MAX + 2];

	fseek(fd, 0, SEEK_SET);
	for(count = 0;;count++){
		char *buf_return, *buf_turn;
		
		buf_return = fgets(buf, sizeof buf, fd);
		if(buf_return == NULL){
			count = -1;
			break;
		}

		buf_turn = strchr(buf, '\n');
		if(buf_turn)
			*(buf_turn) = '\0';
		puts(buf);
		if(strcmp(buf, str) == 0)
			break;
	}

	return count; 
}

int take_line(FILE *fd, int line_num, char *line){
	int i;
	char buf[NAME_MAX + 1], *p;

	fseek(fd, 0, SEEK_SET);
	for(i=0;i <= line_num;i++){
		if(fgets(buf, sizeof buf, fd) == NULL)
			return -1;
	}
	
	p = strchr(buf, '\n');
	if(p)
		*p = '\0';
	strcpy(line, buf);
	
	return 0;
}

struct threads *threads_put(char *receiver, char *transmitter, int sock){
	struct threads *new_thread;

	if((new_thread = (struct threads *)malloc(sizeof(struct threads))) == NULL){
		fputs("malloc err\n", stderr);
		return NULL;
	}
	strcpy(new_thread->receiver, receiver);
	strcpy(new_thread->transmitter, transmitter);
	new_thread->sock = sock;
	new_thread->check[0] = 0;
	new_thread->check[1] = 0;
	new_thread->next = threads_front;
	threads_front = new_thread;

	return new_thread;
}

struct threads *threads_select(char *receiver, char *transmitter){
	struct threads *p;

	for(p = threads_front;p != NULL;p = p->next){
		if(strcmp(p->receiver, receiver) == 0 && strcmp(p->transmitter, transmitter) == 0){
			return p;
		}
	}

	return NULL;
}

int sock_check(int sock){
	struct pollfd fds[1];
	
	fds[0].fd = sock;
	fds[0].events = POLLRDHUP;
	if(poll(fds, 1, 10) == -1)
		return -1;
	if(fds[0].revents & POLLRDHUP)
		return 1;
	return 0;
}

int file2sock(FILE *fd, int sock){
	int i;
	char buf[2048];
	
	fseek(fd, 0, SEEK_SET);
	puts("ファイルの送信を開始します");
	for(i=0;;i++){
		int num;
		num = fgetc(fd);
		if(num == EOF)
			break;
		buf[i] = (char)num;
		if(num == '.'){
			char buf_int[4];
			
			buf[i+1] = '\0';
			int2str(i+2, buf_int);
			write(sock, buf_int, sizeof(int));
			printf("%s: を書き込みます\n", buf);
			write(sock, buf, i+2);
			fgetc(fd);fgetc(fd);
			i = -1;
			continue;
		}
	}
	puts("ファイルの送信が完了しました");

	return 0;
}

int user_list(FILE *user_fd, FILE *pass_fd, char *user, char *pass){
	char name[NAME_MAX + 5];
	
	fseek(user_fd, 0, SEEK_END);
	fseek(pass_fd, 0, SEEK_END);

	fprintf(user_fd, "%s\n", user);
	fprintf(pass_fd, "%s\n", pass);

	strcpy(name, "/usr/users/");
	strcat(name, user);
	mkdir(name, 744);

	return 0;
}
