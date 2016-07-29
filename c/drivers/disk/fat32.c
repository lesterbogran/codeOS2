const char *fat32sig = "FAT32   ";
fat32part currentfat32part;
bool isPartitionFAT32(int disk, int sect){
	readSector(disk, sect, buf);
	bool flag = true;
	for(int i = 0; i < 8; i++){
		if(buf[0x52+i] != fat32sig[i])
			flag = false;
	}
	return flag;
}

fat32part getFat32Part(int disk, int part_sect){
	fat32part temp;
	temp.disk = disk;
	temp.part_sect = part_sect;
	readSector(disk, part_sect, buf);
	for(int i = 0; i < 512; i++){
		if(i == 0xD){
			temp.sectors_per_cluster = buf[i];
		}
		if(i >= 0xE && i <= 0xF){
			if(i == 0x0E){
				temp.reserved_sectors = 0;
			}
			temp.reserved_sectors += ((uint16_t)buf[i]) << (i-0xE)*8;
		}
		if(i == 0x10){
			temp.num_fats = buf[i];
		}
		if(i >= 0x24 && i <=0x28){
			if(i == 0x24){
				temp.sectors_per_fat = 0;
			}
			if(i >= 0x24 && i <= 0x25){
				temp.sectors_per_fat += ((uint32_t)buf[i]) << (i-0x24)*8;
			}else{
				temp.sectors_per_fat += ((uint32_t)buf[i]) << (i-0x27)*8+16;
			}
		}
		if(i >= 0x2C && i <= 0x2F){
			if(i == 0x2C){
				temp.root_dir_clust = 0;
			}
			if(i >= 0x2C && i <= 0x2D){
				temp.root_dir_clust += ((uint32_t)buf[i]) << (i-0x2C)*8;
			}else{
				temp.root_dir_clust += ((uint32_t)buf[i]) << (i-0x2E)*8+16;
			}
		}
	}
	temp.cluster_begin_sect = part_sect + temp.reserved_sectors + (temp.num_fats * temp.sectors_per_fat);
	temp.root_dir_sect = clusterToLBAOther(temp, temp.root_dir_clust);
	temp.fat_sect = part_sect + temp.reserved_sectors;
	temp.current_dir_clust = temp.root_dir_clust;
	return temp;
}

void listDir(uint32_t cluster){
	bool done = false;
	uint32_t ccluster = cluster;
	uint32_t sector = clusterToLBA(ccluster);
	uint32_t fsector = sector;
	uint16_t dir_size = 0;
	while(!done){
		readSector(currentfat32part.disk, sector, buf);
		for(uint8_t i = 0; i < 16; i++){
			uint16_t loc = 0x20*i;
			if(buf[loc] == 0){ //End of directory
				done = true;
				i = 16;
			}else if(buf[loc] != 0xE5 /*Is not unused*/ 
					&& (buf[loc+0xB] & 0xF) != 0xF /*Is not a long filename entry*/ 
					&& (buf[loc+0xB] & 0xA) == 0 /*Is a file or directory and should be shown*/){
				dir_size++;
				uint8_t lastChar = 0;
				for(int i = 0; i < 8; i++){
					if(buf[loc+i] != ' '){
						lastChar = i;
					}
				}
				if(buf[loc+0xB] & 0x10){ //Is a directory
					char name[lastChar+2];
					memcpy(&name, &buf[loc], lastChar+1);
					name[lastChar+1] = 0;
					print(name);
					for(uint8_t j = 0; j <= 13-(lastChar+1); j++){
						print(" ");
					}
					println("<DIR>");
				}else{ //Is a file
					char name[lastChar+6];
					memcpy(&name, &buf[loc], lastChar+1);
					memcpy(&name[lastChar+1], &buf[loc+7], 4);
					name[lastChar+5] = 0;
					name[lastChar+1] = '.';
					println(name);
				}
			}else{
				
			}
		}
		sector++;
		if((sector - fsector) % currentfat32part.sectors_per_cluster == 0){
			ccluster = getNextCluster(ccluster);
			sector = clusterToLBA(ccluster);
			fsector = sector;
		}
	}
}

void listCurrentDir(){
	listDir(currentfat32part.current_dir_clust);
}

uint8_t changeDir(char *dir){ //Returns 0 if success, 1 if is not a directory, and 2 if doesn't exist.
	int index = indexOf('/',dir);
	uint32_t prevClust = currentfat32part.current_dir_clust;
	int s = 0;
	int i = 0;
	if(index != strlen(dir)){ //If it has a '/' in it
		char dirbuf[strlen(dir)];
		if(index == 0){ //If it starts with '/', this means start at the root directory.
			currentfat32part.current_dir_clust = currentfat32part.root_dir_clust;
			s = 1;
			i++;
		}
		bool done = false;
		while(!done){
			index = indexOfn('/', i, dir);
			substrr(s, index, dir, dirbuf);
			if(index == strlen(dir)){
				done = true;
			}
			if(s == strlen(dir))
				break;
			switch(changeOneDir(dirbuf)){
				case 0:
					i++;
					s = index+1;
					break;
				case 1:
					currentfat32part.current_dir_clust = prevClust;
					return 1;
				case 2:
					currentfat32part.current_dir_clust = prevClust;
					return 2;
			}
		}
		return 0;
	}else{
		return changeOneDir(dir);
	}
}

static uint8_t changeOneDir(char *dir){ //Returns 0 if success, 1 if is not a directory, and 2 if doesn't exist.
	char folder[indexOf('/',dir)+1];
	substr(indexOf('/',dir), dir, folder);
	fat32file file = getFile(folder);
	if(exists(file)){
		if(isDirectory(file)){
			if(file.cluster >= currentfat32part.root_dir_clust){
				currentfat32part.current_dir_clust = file.cluster;
			}else{
				currentfat32part.current_dir_clust = currentfat32part.root_dir_clust;
			}
			return 0;
		}else{
			return 1;
		}
	}else{
		return 2;
	}
}

fat32file getFile(char *file){ //If file doesn't have any clusters allocated to it, cluster == 0 and if it doesn't exist, cluster = 0xFFFFFFFF
	bool done = false;
	uint32_t ccluster = currentfat32part.current_dir_clust;
	uint32_t sector = clusterToLBA(ccluster);
	uint32_t fsector = sector;
	uint32_t intbuf = 0;
	fat32file ret = {0xFFFFFFFF,0,0};
	while(!done){
		readSector(currentfat32part.disk, sector, buf);
		for(uint8_t i = 0; i < 16; i++){
			uint16_t loc = 0x20*i;
			if(buf[loc] == 0){ //End of directory
				done = true;
				i = 16;
			}else if(buf[loc] != 0xE5 /*Is not unused*/ 
					&& (buf[loc+0xB] & 0xF) != 0xF /*Is not a long filename entry*/ 
					&& (buf[loc+0xB] & 0x8) == 0 /*Is a file or directory*/){
				uint8_t lastChar = 0;
				for(int i = 0; i < 8; i++){
					if(buf[loc+i] != ' '){
						lastChar = i;
					}
				}
				if(buf[loc+0xB] & 0x10){ //Is a directory
					char name[lastChar+2];
					memcpy(&name, &buf[loc], lastChar+1);
					name[lastChar+1] = 0;
					if(strcmp(name,file) && !done){
						intbuf = 0;
						intbuf += ((uint32_t)buf[loc+0x14] << 16);
						intbuf += ((uint32_t)buf[loc+0x15] << 24);
						intbuf += ((uint32_t)buf[loc+0x1A]);
						intbuf += ((uint32_t)buf[loc+0x1B] << 8);
						ret.cluster = intbuf;
						intbuf = 0;
						intbuf += ((uint32_t)buf[loc+0x1C]);
						intbuf += ((uint32_t)buf[loc+0x1D] << 8);
						intbuf += ((uint32_t)buf[loc+0x1E] << 16);
						intbuf += ((uint32_t)buf[loc+0x1F] << 24);
						ret.size = intbuf;
						ret.dir_cluster = currentfat32part.current_dir_clust;
						ret.attrib = buf[loc+0xB];
						done = true;
					}
				}else{ //Is a file
					char name[lastChar+6];
					memcpy(&name, &buf[loc], lastChar+1);
					memcpy(&name[lastChar+1], &buf[loc+7], 4);
					name[lastChar+5] = 0;
					name[lastChar+1] = '.';
					if(strcmp(name,file) && !done){
						intbuf = 0;
						intbuf += ((uint32_t)buf[loc+0x14] << 16);
						intbuf += ((uint32_t)buf[loc+0x15] << 24);
						intbuf += ((uint32_t)buf[loc+0x1A]);
						intbuf += ((uint32_t)buf[loc+0x1B] << 8);
						ret.cluster = intbuf;
						intbuf = 0;
						intbuf += ((uint32_t)buf[loc+0x1C]);
						intbuf += ((uint32_t)buf[loc+0x1D] << 8);
						intbuf += ((uint32_t)buf[loc+0x1E] << 16);
						intbuf += ((uint32_t)buf[loc+0x1F] << 24);
						ret.size = intbuf;
						ret.dir_cluster = currentfat32part.current_dir_clust;
						ret.attrib = buf[loc+0xB];
						done = true;
					}
				}
			}else{
				
			}
		}
		sector++;
		if((sector - fsector) % currentfat32part.sectors_per_cluster == 0){
			ccluster = getNextCluster(ccluster);
			sector = clusterToLBA(ccluster);
			fsector = sector;
		}
	}
	return ret;
}

bool isDirectory(fat32file file){
	return file.attrib & 0x10;
}

bool exists(fat32file file){
	return file.cluster != 0xFFFFFFFF;
}

uint32_t getClusterChainSize(uint32_t cluster){
	bool done = false;
	uint8_t ccluster = cluster;
	uint8_t cbyte;
	uint32_t nextCluster = 0;
	uint32_t chain_size = 0;
	while(!done){
		chain_size++;
		nextCluster = 0;
		readSector(currentfat32part.disk, getFATSectorForCluster(ccluster), buf2);
		cbyte = (ccluster % 128)*4;
		nextCluster += ((uint32_t)buf2[cbyte]);
		nextCluster += ((uint32_t)buf2[cbyte+1] << 8);
		nextCluster += ((uint32_t)buf2[cbyte+2] << 16);
		nextCluster += (((uint32_t)buf2[cbyte+3] & 0xF) << 24); // & 0xF is because we are ignoring the top 4 bits because they are reserved
		if(nextCluster >= 0x0FFFFFF8){
			done = true;
		}
		ccluster = nextCluster;
	}
	return chain_size;
}

uint32_t getNextCluster(uint32_t cluster){
	readSector(currentfat32part.disk, getFATSectorForCluster(cluster), buf2);
	uint8_t cbyte = (cluster % 128)*4;
	uint32_t ret = 0;
	ret += ((uint32_t)buf2[cbyte]);
	ret += ((uint32_t)buf2[cbyte+1] << 8);
	ret += ((uint32_t)buf2[cbyte+2] << 16);
	ret += (((uint32_t)buf2[cbyte+3] & 0xF) << 24); // & 0xF is because we are ignoring the top 4 bits because they are reserved
	return ret;
}

uint32_t getFATSectorForCluster(uint32_t cluster){
	return currentfat32part.fat_sect+((cluster * 4) / 512);
}

uint32_t getClusterOfEntry(uint8_t *entry){
	return (((uint32_t)(entry[0x1A])))+(((uint32_t)(entry[0x1B])) << 8)+(((uint32_t)(entry[0x14])) << 16)+(((uint32_t)(entry[0x15])) << 24);
}

void setCurrentFat32part(fat32part p){
	currentfat32part = p;
}

uint32_t clusterToLBA(uint32_t cluster){
	return currentfat32part.cluster_begin_sect+(currentfat32part.sectors_per_cluster * (cluster-2));
}

uint32_t clusterToLBAOther(fat32part p, uint32_t cluster){
	return p.cluster_begin_sect+(p.sectors_per_cluster * (cluster-2));
}