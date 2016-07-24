int xpos = 0;
int ypos = 0;
char ccolor = 0x07;

// Specific mode print_util should implement:
// putch_color(char c, char color);
// clearScreen();
// center_print(char c, char color); (Should provide a wrapper for center_print_base(c,color,width))

void putch(char c){
	putch_color(c, ccolor);
}

void print_color(char* c, char color){
	int i = 0;
	while(c[i] != 0){
		putch_color(c[i], color);
		i++;
	}
}

void print(char* c){
	print_color(c, ccolor);
}

void println_color(char* c, char color){
	print_color(c, color);
	print_color("\n", color);
}

void println(char* c){
	println_color(c,ccolor);
}

void setColor(char color){
	ccolor = color;
}

void center_print_base(char* c, char color, int width){
	if(xpos > 0){
		print_color("\n",color);
	}
	int i = 0;
	while(c[i]){
		i++;
	}
	if(i > width){
		print_color(c,color);
	}else{
		if(i % 2 == 0){
			int h = (width-i)/2;
			int j = 0;
			while(j < h){
				putch_color(' ', color);
				j++;
			}
			print_color(c,color);
			j = 0;
			while(j < h){
				putch_color(' ', color);
				j++;
			}
		}else{
			int h = (width-i)/2;
			int j = 0;
			while(j < h){
				putch_color(' ', color);
				j++;
			}
			print_color(c,color);
			j = 0;
			h--;
			while(j < h+2){
				putch_color(' ', color);
				j++;
			}
		}
	}
}

void printHex(uint8_t num){
	char *str = "  ";
	numToHexString(num, str);
	print("0x");
	print(str);
}