int int2str(int num, char *str){
	int i;
	for(i=0;i<sizeof(int);i++){
		int buf;

		buf = num;
		buf >> 8*i;
		buf &= 255;
		str[i] = (char)buf;
	}
	return 0;
}

int str2int(char *str){
	int i, num;

	for(i=0;i<sizeof(int);i++)
		num += (str[i] << 8*i);

	return num;
}
