/*
 *  Copyright (C) 2023 CS416 Rutgers CS
 *	Tiny File System
 *	File:	rufs.c
 *
 *	Author: Yash Patel | yp315
 *	Author: Kev Sharma | kks107
 * 
 *	Tested on ls.cs.rutgers.edu
 */

#define FUSE_USE_VERSION 26

#include <fuse.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <errno.h>
#include <sys/time.h>
#include <libgen.h>
#include <limits.h>
#include <sys/types.h>

#include "block.h"
#include "rufs.h"

char diskfile_path[PATH_MAX];

// total inode blocks 64
size_t NUM_INODE_BLKS = (MAX_INUM)/(BLOCK_SIZE/sizeof(struct inode));
size_t INODE_BITMAP_BYTES = MAX_INUM / 8;
size_t DATA_BLOCK_BITMAP_BYTES = MAX_DNUM / 8;
size_t DIRENTS_IN_BLOCK = BLOCK_SIZE/sizeof(struct dirent);
int ROOT_DIR_INO;

// Declare your in-memory data structures here
struct superblock* superblock;
bitmap_t inode_bitmap;
bitmap_t data_block_bitmap;


/* 
 * Get available inode number from bitmap
 */
int get_avail_ino() {

	// Step 1: Read inode bitmap from disk
	bitmap_t* buf = malloc(BLOCK_SIZE);
	if(bio_read(superblock->i_bitmap_blk, buf) <= 0){
		perror("RUFS: failed to read from block exiting\n");
		free(buf);
		exit(1);
	}
	memcpy(inode_bitmap,buf,INODE_BITMAP_BYTES); //copys bitmap bytes to local bitmap
	// Step 2: Traverse inode bitmap to find an available slot
	int ret = -1;
	int i;
	for (i = 0; i < MAX_INUM; ++i){
		if(!get_bitmap(inode_bitmap,i)){
			ret = i;
			break;
		}
	}
	// Step 3: Update inode bitmap and write to disk 
	if(ret >= 0){
		set_bitmap(inode_bitmap,ret);
		memcpy(buf,inode_bitmap,INODE_BITMAP_BYTES); //copys bitmap bytes to buf
		if(bio_write(superblock->i_bitmap_blk,buf) < 0){
			perror("RUFS: failed to write to the block exiting\n");
			free(buf);
			exit(1);
		}
	}
	free(buf);
	return ret;
}

/* 
 * Get available data block number from bitmap
 */
int get_avail_blkno() {

	// Step 1: Read data block bitmap from disk
	bitmap_t* buf = malloc(BLOCK_SIZE);
	if(bio_read(superblock->d_bitmap_blk, buf) <= 0){
		perror("RUFS: failed to read from data bitmap block\nexiting\n");
		exit(1);
	}
	memcpy(data_block_bitmap,buf,DATA_BLOCK_BITMAP_BYTES); //copys bitmap bytes to local bitmap
	// Step 2: Traverse data block bitmap to find an available slot
	int ret = -1;
	int i;
	for (i = 0; i < MAX_DNUM; ++i){
		if(!get_bitmap(data_block_bitmap,i)){
			ret = i;
			break;
		}
	}

	// Step 3: Update data block bitmap and write to disk 
	if(ret >= 0){
		set_bitmap(data_block_bitmap,ret);
		memcpy(buf,data_block_bitmap,DATA_BLOCK_BITMAP_BYTES);
		if(bio_write(superblock->d_bitmap_blk,buf) < 0){
			perror("RUFS: failed to write to the data bitmap block exiting\n");
			exit(1);
		}
	}
	
	return ret;
}

/* 
 * inode operations
 */
int readi(uint16_t ino, struct inode *inode) {

	// Step 1: Get the inode's on-disk block number
	// formula that gives the index of the block then we add where we start inodes.
	int blk = ((ino * sizeof(struct inode))/BLOCK_SIZE) + superblock->i_start_blk;

	// Step 2: Get offset of the inode in the inode on-disk block
	int offset = ino % (BLOCK_SIZE/sizeof(struct inode));

	// Step 3: Read the block from disk and then copy into inode structure
	struct inode* buf = (struct inode*) malloc(BLOCK_SIZE);

	bio_read(blk,buf); //TODO add error check

	struct inode temp = buf[offset];
	memcpy(inode,&temp,sizeof(struct inode));

	free(buf);
	return 1;
}

int writei(uint16_t ino, struct inode *inode) {

	// Step 1: Get the block number where this inode resides on disk
	// formula that gives the index of the block then we add where we start inodes.
	int blk = ((ino * sizeof(struct inode))/BLOCK_SIZE) + superblock->i_start_blk;
	
	// Step 2: Get the offset in the block where this inode resides on disk
	int offset = ino % (BLOCK_SIZE/sizeof(struct inode));

	// Step 3: Write inode to disk 
	struct inode* buf = (struct inode*) malloc(BLOCK_SIZE);

	bio_read(blk,buf);

	buf[offset] = *(inode);
	//TODO look into stat stuff if that is needed

	bio_write(blk,buf);
	free(buf);
	return 1;
}


/* 
 * directory operations
 */
int dir_find(uint16_t ino, const char *fname, size_t name_len, struct dirent *dirent) {

  // Step 1: Call readi() to get the inode using ino (inode number of current directory)
  //printf("finding fname: %s\n", fname);
  struct inode dir_inode;
  readi(ino,&dir_inode);

  // if the inode cant be found or inode is invalid return not found
  if(!dir_inode.valid){
	return 0;
  }

  struct dirent* buf = (struct dirent*) malloc(BLOCK_SIZE);

  // Step 2: Get data block of current directory from inode
  // 16 direct ptrs
  // using 16 hard coded because it hard coded for inode and too lazy
  for(int i = 0; i < 16; ++i){
	if(dir_inode.direct_ptr[i] != INVALID){
		bio_read(dir_inode.direct_ptr[i],buf);

		for(int j = 0; j < DIRENTS_IN_BLOCK; ++j){
			struct dirent curr = buf[j];
			if(curr.valid){
				//printf("j: %d\n", j);
				// Step 3: Read directory's data block and check each directory entry.
  				//If the name matches, then copy directory entry to dirent structure
				if(strcmp(fname,curr.name) == 0){
					//printf("same name j: %d\n", j);
					memcpy(dirent,&curr,sizeof(struct dirent));
					free(buf);
					return 1;
				}
			}
		}
	}
  }

	free(buf);
	return 0;
}

// ret values -1 no more space, 0 file already exists, 1 added successfully
// TODO verify logic
int dir_add(struct inode dir_inode, uint16_t f_ino, const char *fname, size_t name_len) {

	// Step 1: Read dir_inode's data block and check each directory entry of dir_inode
	// Step 2: Check if fname (directory name) is already used in other entries
	// Step 3: Add directory entry in dir_inode's data block and write to disk
	// Allocate a new data block for this directory if it does not exist
	// Update directory inode
	// Write directory entry

	//if the inode is not valid we cant add
	if(!dir_inode.valid){
		return 0;
	}

	// Search if there exist an fname that is similar if there is we cant add
	struct dirent* buf = (struct dirent*) malloc(BLOCK_SIZE);
	for(int i = 0; i < 16; ++i){
		if(dir_inode.direct_ptr[i] == VALID){
			bio_read(dir_inode.direct_ptr[i],buf);
		
			for(int j = 0; j < DIRENTS_IN_BLOCK; ++j){
				struct dirent curr = buf[j];
				if(curr.valid){
					if(strcmp(fname,curr.name) == 0){
						free(buf);
						return 0; // found another entry with same name
					}
				}
			}
		}
	}

	// find an empty dirent
	int new_dirent_index = -1;
	int ptr_index = -1;
	int new_dirent_blk = 0; // boolean if we need to get new block that is full of dirents
	int found_spot = 0;
	
	for(int dir_ptr_index = 0; dir_ptr_index < 16; ++dir_ptr_index){
		// check if a direct pointer is not allocated 
		// if not we make a block of dirents
		int ptr_blk = dir_inode.direct_ptr[dir_ptr_index];
		if(ptr_blk == INVALID){
			new_dirent_blk = 1;
			ptr_index = dir_ptr_index;
			found_spot = 1;
			break;
		}
		// if the block is allocated check if the block of dirents has space
		bio_read(ptr_blk,buf);
		for(int dirent_index = 0; dirent_index < DIRENTS_IN_BLOCK; ++dirent_index){
			struct dirent curr = buf[dirent_index];
			//printf("curr index: %d -> isvalid: %d\n",dirent_index, curr.valid);
			if(!curr.valid){
				new_dirent_index = dirent_index;
				ptr_index = dir_ptr_index;
				found_spot = 1;
				break;
			}
		}
		if(found_spot){
			break; // breaks out of out for loop if we found a spot
		}
	}

	if(!found_spot){
		free(buf);
		return -1; //no space for new dir entry
	}

	//printf("adding %s, new_dirent_blk %d, ptr_index: %d, new_dirent_index: %d\n",fname,new_dirent_blk,ptr_index,new_dirent_index);
	
	// if we need to make a new block get a new block and format it for dirents
	if(new_dirent_blk){
		int new_blk = get_avail_blkno();
		struct dirent* buf_dirent_blk = (struct dirent*) malloc(BLOCK_SIZE);
		memset(buf_dirent_blk,0,BLOCK_SIZE);
		
		// creates a new block of dirents and sets them to invalid
		for(int i = 0; i < DIRENTS_IN_BLOCK; ++i){
			buf_dirent_blk[i].valid = INVALID;
		}

		buf_dirent_blk[0].ino = f_ino;
		buf_dirent_blk[0].len = name_len;
		strcpy(buf_dirent_blk[0].name,fname);
		buf_dirent_blk[0].valid = VALID;
		bio_write(new_blk,buf_dirent_blk);
		dir_inode.direct_ptr[ptr_index] = new_blk;

		writei(dir_inode.ino,&dir_inode);
		free(buf_dirent_blk);
	}
	else{
		int blk_num = dir_inode.direct_ptr[ptr_index];
		bio_read(blk_num,buf);
		buf[new_dirent_index].ino = f_ino;
		buf[new_dirent_index].len = name_len;
		strcpy(buf[new_dirent_index].name,fname);
		buf[new_dirent_index].valid = VALID;

		bio_write(blk_num,buf);
	}
	free(buf);

	return 1; // added entry to dir
}

int dir_remove(struct inode dir_inode, const char *fname, size_t name_len) {
	// Not needed due to time
	// Step 1: Read dir_inode's data block and checks each directory entry of dir_inode
	
	// Step 2: Check if fname exist

	// Step 3: If exist, then remove it from dir_inode's data block and write to disk

	return 0;
}

/* 
 * namei operation
 */
int get_node_by_path(const char *path, uint16_t ino, struct inode *inode) {
	
	// Step 1: Resolve the path name, walk through path, and finally, find its inode.
	// Note: You could either implement it in a iterative way or recursive way

	char delim[2] = "/";
	char* str = strtok((char*)path,delim);

	struct dirent* nodei = (struct dirent*) malloc(sizeof(struct dirent));
	nodei->ino = ino; 

	if(strcmp(path,"/") == 0){
		readi(ROOT_DIR_INO,inode);
		return 0;
	}

	// go through tokens to find right dir entry
	while(str != NULL){
		if(dir_find(nodei->ino,str,strlen(str)+1,nodei) == 0){
			return 1; // cant find it wrong path/doesnt exist
		}
		str = strtok(NULL,delim);
	}

	// get dir entry inode and read it
	readi(nodei->ino,inode);

	return 0;
}

/* 
 * Make file system
 */
int rufs_mkfs() {

	// Call dev_init() to initialize (Create) Diskfile
	dev_init(diskfile_path);

	// write superblock information
	superblock = malloc(sizeof(struct superblock));
	memset(superblock,0,sizeof(struct superblock));

	superblock->magic_num = MAGIC_NUM;
	superblock->max_inum = MAX_INUM;
	superblock->max_dnum = MAX_DNUM;
	superblock->i_bitmap_blk = 1; // 1 index
	superblock->d_bitmap_blk = (superblock->i_bitmap_blk) + 1; //1 block for bitmap
	superblock->i_start_blk = (superblock->d_bitmap_blk) + 1; //1 block for bitmap
	superblock->d_start_blk = (superblock->i_start_blk) + NUM_INODE_BLKS; // 64 blocks for INODES

	struct superblock* buf = (struct superblock*) malloc(BLOCK_SIZE);
	memcpy(buf,superblock,sizeof(struct superblock));
	bio_write(0,buf);
	free(buf);

	// initialize inode bitmap
	inode_bitmap = (bitmap_t) malloc(INODE_BITMAP_BYTES);
	memset(inode_bitmap, 0, INODE_BITMAP_BYTES);
	
	// initialize data block bitmap
	data_block_bitmap = (bitmap_t) malloc(DATA_BLOCK_BITMAP_BYTES);
	memset(data_block_bitmap, 0, DATA_BLOCK_BITMAP_BYTES);
	
	//Temp Inode that get written into disk
	struct inode* temp_inode = malloc(sizeof(struct inode));
	memset(temp_inode,0,sizeof(struct inode));
	temp_inode->valid = INVALID;

	for(int i = 0; i < 16; ++i){
		temp_inode->direct_ptr[i] = 0;
	}

	//Changes the Inode number to i and writes to disk
	// keeping it this way but if we are looking to be efficient we would block them together and then write to disk
	// so we would make 64 writes instead of 1024
	for(int i = 1; i < MAX_INUM; ++i){
		temp_inode->ino = i;
		writei(i,temp_inode);
	}
	free(temp_inode);

	//datablocks occupied by metadata 0...67 0,1,2 for superblock and bitmap rest 3...67 for inodes
	for(int i = 0; i < superblock->d_start_blk; ++i){
		set_bitmap(data_block_bitmap,i);
	}

	// update bitmap information for root directory
	struct bitmap_t* bitmap_buf = (struct bitmap_t*) malloc(BLOCK_SIZE);
	memset(bitmap_buf,0,BLOCK_SIZE);
	memcpy(bitmap_buf,inode_bitmap,INODE_BITMAP_BYTES);
	bio_write(superblock->i_bitmap_blk, (void*) bitmap_buf);
	memset(bitmap_buf,0,BLOCK_SIZE);
	memcpy(bitmap_buf,data_block_bitmap,DATA_BLOCK_BITMAP_BYTES);
	bio_write(superblock->d_bitmap_blk, (void*) bitmap_buf);
	free(bitmap_buf);

	// update inode for root directory
	// create inode for root directory "/" with inode number 0 which contains directory entries
	// Other FS keep other inodes for other reasons but we can use 0th ino for "/" for rufs
	struct inode root_dir;
	int root_dir_ino = get_avail_ino();
	readi(root_dir_ino,&root_dir);
	
	root_dir.ino = root_dir_ino;
	root_dir.valid = VALID;
	root_dir.type = DIRECTORY;
	root_dir.link = 2;
	for(int i = 0; i < 16; ++i){
		root_dir.direct_ptr[i] = INVALID;
	}
	//No indirect ptrs for this proj
	root_dir.vstat.st_mode = S_IFDIR | 0755;
	time(&root_dir.vstat.st_atime);
	time(&root_dir.vstat.st_mtime);

	writei(root_dir_ino,&root_dir);
	ROOT_DIR_INO = root_dir_ino;

	//default 2 links
	dir_add(root_dir,ROOT_DIR_INO,".",strlen(".")+1); // current dir
	readi(ROOT_DIR_INO,&root_dir);
	dir_add(root_dir,ROOT_DIR_INO,"..",strlen("..")+1); //parent dir root doesnt have parent so points to it self

	return 0;
}


/* 
 * FUSE file operations
 */
static void *rufs_init(struct fuse_conn_info *conn) {

	// Step 1a: If disk file is not found, call mkfs
	// Step 1b: If disk file is found, just initialize in-memory data structures
  	// and read superblock from disk
	if(dev_open(diskfile_path) < 0){
		//printf("making file system\n");
		rufs_mkfs();
	}
	else{
		superblock = malloc(sizeof(struct superblock));
		memset(superblock,0,sizeof(struct superblock));

		inode_bitmap = (bitmap_t) malloc(INODE_BITMAP_BYTES);
		memset(inode_bitmap, 0, INODE_BITMAP_BYTES);

		data_block_bitmap = (bitmap_t) malloc(DATA_BLOCK_BITMAP_BYTES);
		memset(data_block_bitmap, 0, DATA_BLOCK_BITMAP_BYTES);

		struct superblock* buf = (struct superblock*) malloc(BLOCK_SIZE);
		bio_read(0,buf);
		memcpy(superblock,buf,sizeof(struct superblock));
		free(buf);
		ROOT_DIR_INO = 0;
		// asked to not read the bit map
		// bio_read(superblock->i_bitmap_blk,inode_bitmap);
		// bio_read(superblock->d_bitmap_blk,data_block_bitmap);
	}

	return NULL;
}

static void rufs_destroy(void *userdata) {

	// Step 1: De-allocate in-memory data structures
	free(superblock);
	free(inode_bitmap);
	free(data_block_bitmap);

	// Step 2: Close diskfile
	dev_close();
}

static int rufs_getattr(const char *path, struct stat *stbuf) {

	// Step 1: call get_node_by_path() to get inode from path
	// Step 2: fill attribute of file into stbuf from inode

		// stbuf->st_mode   = S_IFDIR | 0755;
		// stbuf->st_nlink  = 2;
		// time(&stbuf->st_mtime);
	struct inode* nodei = (struct inode*) malloc(sizeof(struct inode));

	if(get_node_by_path(path,ROOT_DIR_INO,nodei)){
		return -ENOENT; //TODO check this
	}

	// copy inodes stat to stbuf
	*stbuf = nodei->vstat;
	stbuf->st_size = nodei->size;
	free(nodei);
	return 0; // success
}

static int rufs_opendir(const char *path, struct fuse_file_info *fi) {

	// Step 1: Call get_node_by_path() to get inode from path
	// Step 2: If not find, return -1

	struct inode* nodei = (struct inode*) malloc(sizeof(struct inode));
	
	if(get_node_by_path(path,ROOT_DIR_INO,nodei)){
		return -1;
	}

	return 0; //success
}

static int rufs_readdir(const char *path, void *buffer, fuse_fill_dir_t filler, off_t offset, struct fuse_file_info *fi) {

	// Step 1: Call get_node_by_path() to get inode from path

	// Step 2: Read directory entries from its data blocks, and copy them to filler

	struct inode* nodei = (struct inode*) malloc(sizeof(struct inode));

	if(get_node_by_path(path,ROOT_DIR_INO,nodei)){
		return -ENOENT;
	}

	for(int i = 0; i < 16; ++i){
		if(nodei->direct_ptr[i] != INVALID){
			struct dirent* buf = (struct dirent*) malloc(BLOCK_SIZE);

			bio_read(nodei->direct_ptr[i],buf);

			for(int j = 0; j < DIRENTS_IN_BLOCK; ++j){
				if(buf[j].valid){
					if(filler(buffer,buf[j].name,NULL,0) != 0){
						return -ENOMEM;
					}
				}
			}
		}
	}

	return 0;
}


static int rufs_mkdir(const char *path, mode_t mode) {

	// Step 1: Use dirname() and basename() to separate parent directory path and target directory name

	// Step 2: Call get_node_by_path() to get inode of parent directory

	// Step 3: Call get_avail_ino() to get an available inode number

	// Step 4: Call dir_add() to add directory entry of target directory to parent directory

	// Step 5: Update inode for target directory

	// Step 6: Call writei() to write inode to disk

	char * dirname_path = (char *)malloc(strlen(path) + 1) ;
	strcpy(dirname_path, path) ;
	dirname(dirname_path) ;

	char * basename_path = (char *)malloc(strlen(path) + 1) ;
	strcpy(basename_path, path) ;
	basename_path = basename(basename_path) ;

	struct inode* nodei = (struct inode*) malloc(sizeof(struct inode));

	if(get_node_by_path(dirname_path,ROOT_DIR_INO,nodei)){
		return -ENOENT;
	}

	int new_ino = get_avail_ino();

	int ret_dir_add = dir_add(*nodei,new_ino, basename_path,strlen(basename_path) + 1);

	if(ret_dir_add == -1){
		return -EFBIG; // File too large there are not anymore direct ptr
	}
	if(ret_dir_add == 0){
		return -EEXIST;
	}
	
	struct inode* new_dirent = (struct inode*) malloc(sizeof(struct inode));
	
	readi(new_ino,new_dirent);

	new_dirent->valid = VALID;
	new_dirent->ino = new_ino;
	new_dirent->type = DIRECTORY;
	new_dirent->vstat.st_mode = S_IFDIR | 0755;

	time(&new_dirent->vstat.st_atime);
	time(&new_dirent->vstat.st_mtime);

	for(int i = 0; i < 16; ++i){
		new_dirent->direct_ptr[i] = INVALID;
	}

	writei(new_ino,new_dirent);

	// add relatives to itself and its parent
	dir_add(*new_dirent,new_ino,".",strlen(".") + 1);
	readi(new_ino,new_dirent);
	dir_add(*new_dirent,nodei->ino,"..",strlen("..") + 1);

	// adds to parents link
	readi(nodei->ino,nodei);

	nodei->link += 1;
	nodei->vstat.st_nlink += 1;

	writei(nodei->ino,nodei);

	return 0;
}

static int rufs_rmdir(const char *path) {

	// Step 1: Use dirname() and basename() to separate parent directory path and target directory name

	// Step 2: Call get_node_by_path() to get inode of target directory

	// Step 3: Clear data block bitmap of target directory

	// Step 4: Clear inode bitmap and its data block

	// Step 5: Call get_node_by_path() to get inode of parent directory

	// Step 6: Call dir_remove() to remove directory entry of target directory in its parent directory

	return 0;
}

static int rufs_releasedir(const char *path, struct fuse_file_info *fi) {
	// For this project, you don't need to fill this function
	// But DO NOT DELETE IT!
    return 0;
}

static int rufs_create(const char *path, mode_t mode, struct fuse_file_info *fi) {

	// Step 1: Use dirname() and basename() to separate parent directory path and target file name

	// Step 2: Call get_node_by_path() to get inode of parent directory

	// Step 3: Call get_avail_ino() to get an available inode number

	// Step 4: Call dir_add() to add directory entry of target file to parent directory

	// Step 5: Update inode for target file

	// Step 6: Call writei() to write inode to disk

	char * dirname_path = (char *)malloc(strlen(path) + 1) ;
	strcpy(dirname_path, path) ;
	dirname(dirname_path) ;

	char * basename_path = (char *)malloc(strlen(path) + 1) ;
	strcpy(basename_path, path) ;
	basename_path = basename(basename_path) ;

	struct inode* nodei = (struct inode*) malloc(sizeof(struct inode));

	if(get_node_by_path(dirname_path,ROOT_DIR_INO,nodei)){
		return -ENOENT;
	}

	int new_ino = get_avail_ino();

	int ret_dir_add = dir_add(*nodei,new_ino, basename_path,strlen(basename_path) + 1);

	if(ret_dir_add == -1){
		return -EFBIG; // File too large there are not anymore direct ptr
	}
	if(ret_dir_add == 0){
		return -EEXIST;
	}
	
	struct inode* new_dirent = (struct inode*) malloc(sizeof(struct inode));
	
	readi(new_ino,new_dirent);

	new_dirent->valid = VALID;
	new_dirent->ino = new_ino;
	new_dirent->type = FILE;
	new_dirent->size = 0;
	new_dirent->vstat.st_mode = S_IFREG | 0666;
	for(int i = 0; i < 16; ++i){
		new_dirent->direct_ptr[i] = INVALID;
	}

	time(&new_dirent->vstat.st_atime);
	time(&new_dirent->vstat.st_mtime);

	writei(new_ino,new_dirent);

	return 0;
}

static int rufs_open(const char *path, struct fuse_file_info *fi) {

	// Step 1: Call get_node_by_path() to get inode from path

	// Step 2: If not find, return -1

	struct inode* nodei = (struct inode*) malloc(sizeof(struct inode));
	
	if(get_node_by_path(path,ROOT_DIR_INO,nodei)){
		time(&nodei->vstat.st_atime);
		return -1;
	}

	return 0; //success
}

static int rufs_read(const char *path, char *buffer, size_t size, off_t offset, struct fuse_file_info *fi) {

	// Step 1: You could call get_node_by_path() to get inode from path

	// Step 2: Based on size and offset, read its data blocks from disk

	// Step 3: copy the correct amount of data from offset to buffer

	// Note: this function should return the amount of bytes you copied to buffer

	struct inode* nodei = (struct inode*) malloc(sizeof(struct inode));

	if(get_node_by_path(path,ROOT_DIR_INO,nodei)){
		return -ENOENT;
	}

	if(offset >= nodei->size){
		return 0; // cant read more than size of file
	}

	int blk_index = offset / BLOCK_SIZE;

	int max_file_readable = nodei->size - offset;

	int read_size_remaining = (max_file_readable < size) ? max_file_readable : size;

	int bytes_read = 0;

	char* buf = (char *) malloc(BLOCK_SIZE);

	while(read_size_remaining > 0 && blk_index < 16){
		
		memset(buf,0,BLOCK_SIZE); // reset buf to 0

		int buf_offset = offset % BLOCK_SIZE;

		int blk_size_after_offset = BLOCK_SIZE - buf_offset;

		int read_size = (read_size_remaining <= blk_size_after_offset) ? read_size_remaining : blk_size_after_offset;

		if(nodei->direct_ptr[blk_index] == INVALID){
			// shouldnt get here because it should only loop through readable bytes
			return bytes_read;
		}

		bio_read(nodei->direct_ptr[blk_index],buf);

		memcpy(buffer + bytes_read, buf + buf_offset, read_size);

		offset = 0;
		read_size_remaining -= read_size;
		bytes_read += read_size;
		++blk_index;
	}

	time(&nodei->vstat.st_atime);

	writei(nodei->ino,nodei);

	return bytes_read;
}

static int rufs_write(const char *path, const char *buffer, size_t size, off_t offset, struct fuse_file_info *fi) {
	// Step 1: You could call get_node_by_path() to get inode from path

	// Step 2: Based on size and offset, read its data blocks from disk

	// Step 3: Write the correct amount of data from offset to disk

	// Step 4: Update the inode info and write it to disk

	// Note: this function should return the amount of bytes you write to disk

	struct inode* nodei = (struct inode*) malloc(sizeof(struct inode));

	if(get_node_by_path(path,ROOT_DIR_INO,nodei)){
		return -ENOENT;
	}

	// This is for rewriting because simple test will fail if we dont delete diskfile
	// as we are checking the size so we make size back to 0 so it can rewrite and pass test.
	// if(offset == 0){
	// 	nodei->size = 0;
	// 	nodei->vstat.st_size = 0;
	// }

	int blk_index = offset / BLOCK_SIZE;

	int write_size_remaining = size;

	int bytes_written = 0;

	char* buf = (char *) malloc(BLOCK_SIZE);

	while(write_size_remaining > 0 && blk_index < 16){
		
		memset(buf,0,BLOCK_SIZE); // reset buf to 0

		int buf_offset = offset % BLOCK_SIZE;

		int blk_size_after_offset = BLOCK_SIZE - buf_offset;

		// write what is going to fit in the block
		// either how much we have to write or blk size after offset
		int write_size = (write_size_remaining <= blk_size_after_offset) ? write_size_remaining : blk_size_after_offset;

		//printf("[WRITE]: write size: %d, blk size after ofset: %d, buf offset: %d \n", write_size, blk_size_after_offset, buf_offset);

		if(nodei->direct_ptr[blk_index] == INVALID){
			//printf("[WRITE]: Getting a new BLOCK\n");
			int new_blkno = get_avail_blkno();
			nodei->direct_ptr[blk_index] = new_blkno;
		}

		bio_read(nodei->direct_ptr[blk_index],buf);

		//printf("[WRITE]: getting buffer at: %d\n", nodei->direct_ptr[blk_index]);
		memcpy(buf + buf_offset, buffer + bytes_written,write_size);

		//printf("[WRITE]: BUFFER after %s\n", buf);
		bio_write(nodei->direct_ptr[blk_index],buf);
		offset = 0; // after first write offset is 0 to start right next to previous write
		write_size_remaining -= write_size;
		bytes_written += write_size;
		++blk_index;
	}

	nodei->size += bytes_written;
	nodei->vstat.st_size += bytes_written;

	time(&nodei->vstat.st_atime);
	time(&nodei->vstat.st_mtime);

	writei(nodei->ino,nodei);

	return bytes_written;
}

static int rufs_unlink(const char *path) {

	// Step 1: Use dirname() and basename() to separate parent directory path and target file name

	// Step 2: Call get_node_by_path() to get inode of target file

	// Step 3: Clear data block bitmap of target file

	// Step 4: Clear inode bitmap and its data block

	// Step 5: Call get_node_by_path() to get inode of parent directory

	// Step 6: Call dir_remove() to remove directory entry of target file in its parent directory

	return 0;
}

static int rufs_truncate(const char *path, off_t size) {
	// For this project, you don't need to fill this function
	// But DO NOT DELETE IT!
    return 0;
}

static int rufs_release(const char *path, struct fuse_file_info *fi) {
	// For this project, you don't need to fill this function
	// But DO NOT DELETE IT!
	return 0;
}

static int rufs_flush(const char * path, struct fuse_file_info * fi) {
	// For this project, you don't need to fill this function
	// But DO NOT DELETE IT!
    return 0;
}

static int rufs_utimens(const char *path, const struct timespec tv[2]) {
	// For this project, you don't need to fill this function
	// But DO NOT DELETE IT!
    return 0;
}


static struct fuse_operations rufs_ope = {
	.init		= rufs_init,
	.destroy	= rufs_destroy,

	.getattr	= rufs_getattr,
	.readdir	= rufs_readdir,
	.opendir	= rufs_opendir,
	.releasedir	= rufs_releasedir,
	.mkdir		= rufs_mkdir,
	.rmdir		= rufs_rmdir,

	.create		= rufs_create,
	.open		= rufs_open,
	.read 		= rufs_read,
	.write		= rufs_write,
	.unlink		= rufs_unlink,

	.truncate   = rufs_truncate,
	.flush      = rufs_flush,
	.utimens    = rufs_utimens,
	.release	= rufs_release
};


int main(int argc, char *argv[]) {
	int fuse_stat;

	getcwd(diskfile_path, PATH_MAX);
	strcat(diskfile_path, "/DISKFILE");

	fuse_stat = fuse_main(argc, argv, &rufs_ope, NULL);

	return fuse_stat;
}

