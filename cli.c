#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <netdb.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <pthread.h>
#include <sys/select.h>

#define NAME_MAX 128

int message(int sock, int who);
void *output(void *arg);
void *input(void *arg);
int open_fd(int fd, FILE **finp, FILE **fintp);
int conection(char *host, char *service);

int str2int(char *str);
int int2str(int num, char *str);

//user\npassを送信して、認証に成功したら0,失敗したら-1を返す
int user_check(char *user, char *pass, int sock);
//userを送信して、認証に成功したら0,失敗したら-1を返す
int select_partner(char *user, int sock);
//\0も含めての文字数を出力する
int str_size(char *str);
//fileがあるかチェックする。あれば0,なければ-1を返す
int file_check(char *file);
//:/usr/userファイルを作る.userとpassを記録。
int create_user(char *user, char *pass);
//:/usr/userからuserとpassをと取り出す
int select_user(char *user, char *pass);

int main(int argc, char *argv[]){
	int sock;
	char user[NAME_MAX], pass[NAME_MAX], partner[NAME_MAX];
	struct stat buf;
	FILE *in, *out;
	
	if(argc < 3){
		fprintf(stderr, "%s server port: please!!\n", argv[0]);
		exit(1);
	}
	sock = conection(argv[1], argv[2]);
	
	if(file_check("/usr/user") < 0){
		puts("ユーザーを新規作成します。ユーザー名を入力してください。");
		fgets(user, sizeof user, stdin);
		puts("パスワードを入力してください。");
		fgets(pass, sizeof user, stdin);
		*strchr(user, '\n') = '\0';
		*strchr(pass, '\n') = '\0';
		create_user(user, pass);
		puts("ユーザを作成しました");
	}else{
		select_user(user, pass);
	}

	if(user_check(user, pass, sock) < 0){
		puts("認証に失敗しました");
		exit(1);
	}
	
	puts("通信する相手を選択したください");
	fgets(partner, sizeof partner, stdin);
	if(select_partner(partner, sock) < 0){
		puts("相手が登録していません");
		exit(1);
	}

	message(sock, 1);

	exit(0);
}

//finpがfdの読み込み専用のやつ。fintpは書き込み専用のやつ
int open_fd(int fd, FILE **finp, FILE **fintp){
	int fd2;
	FILE *fin1, *fin2;

	if((fd2 = dup(fd)) < 0){
		fputs("dup: err\n", stderr);
		return 1;
	}
	if((fin1 = fdopen(fd2, "r")) == NULL){
		fputs("fin1 open: err\n", stderr);
		return 1;
	}
	if((fin2 = fdopen(fd, "w")) == NULL){
		fputs("fin2 open: err\n", stderr);
		return 1;
	}
	printf("%d,%d\n",fd, fd2);
	*finp = fin1;
	*fintp = fin2;

	return 0;
}

//hostでipアドレスまたはドメインを指定 serviceでポート番号またはサービス名を指定
int conection(char *host, char *service){
	int sock, err;
	struct addrinfo hints, *res, *ai;

	memset(&hints, 0, sizeof(struct addrinfo));
	hints.ai_family = AF_INET;
	hints.ai_socktype = SOCK_STREAM;
	if((err = getaddrinfo(host, service, &hints, &res)) != 0){
		fprintf(stderr, "getaddrinfo\n");
		exit(1);
	}
	for(ai = res;ai;ai = ai->ai_next){
		sock = socket(ai->ai_family, ai->ai_socktype, ai->ai_protocol);
		if(sock < 0) continue;
		if(connect(sock, ai->ai_addr, ai->ai_addrlen) < 0){
			close(sock);
			continue;
		}
		freeaddrinfo(res);
		return sock;
	}
	fprintf(stderr, "socket(2)/connect(2) failed\n");
	freeaddrinfo(res);
	exit(1);
}

int user_check(char *user, char *pass, int sock){
	int num;
	char buf[NAME_MAX*2 + 3], char_num[4], reply[3];

	strcpy(buf, user);
	strcat(buf, "\n");
	strcat(buf, pass);
	num = str_size(buf);
	int2str(num, char_num);
	write(sock, char_num, sizeof(int));
	write(sock, buf, num);

	read(sock, reply, 3);
	if(strcmp(reply, "OK") == 0)
		return 0;
	else
		return -1;
}

int select_partner(char *user, int sock){
	int num;
	char buf[NAME_MAX + 1], char_num[4], reply[3];

	strcpy(buf, user);
	num = str_size(buf);
	int2str(num, char_num);
	write(sock, char_num, sizeof(int));
	write(sock, buf, num);

	read(sock, reply, 3);
	if(strcmp(reply, "OK") == 0)
		return 0;
	else
		return -1;
}

//\0も含めての文字数を出力する
int str_size(char *str){
	int i;

	for(i=0;str[i] != '\0';i++);

	return i + 1;
}

//fileがあるかチェックする。あれば0,なければ-1を返す
int file_check(char *file){
	struct stat buf;

	return stat(file, &buf);
}

//:/usr/userファイルを作る.userとpassを記録。失敗したら-1を返す
int create_user(char *user, char *pass){
	FILE *fd;

	if((fd = fopen("/usr/user", "a+")) < 0)
		return -1;
	fprintf(fd, "%s\n%s", user, pass);
	fclose(fd);

	return 0;
}

//:/usr/userからuserとpassをと取り出す
int select_user(char *user, char *pass){
	char buf[NAME_MAX + 1], *p;
	FILE *fd;

	if((fd = fopen("/usr/user", "r")) < 0)
		return -1;
	fgets(buf, sizeof buf, fd);
	if(p = strchr(buf, '\n'))
		*p = '\0';
	strcpy(user, buf);
	fgets(buf, sizeof buf, fd);
	if(p = strchr(buf, '\n'))
		*p = '\0';
	strcpy(pass, buf);

	return 0;
}
