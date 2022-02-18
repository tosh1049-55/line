int int2str(int num, char *str){
	int i;
	for(i=0;i<sizeof(int);i++){
		int buf;

		buf = num;
		buf >>= (8*i);
		buf &= 0xff;
		str[i] = (char)buf;
	}
	return 0;
}

int str2int(char *str){
	int i, num = 0;

	for(i=0;i<sizeof(int);i++)
		num += (str[i] << (8*i));

	return num;
}
