/*
 *  Copyright (C) 2023 CS416 Rutgers CS
 *	Tiny File System
 *	File:	block.h
 *
 */

#ifndef _BLOCK_H_
#define _BLOCK_H_

#define BLOCK_SIZE 4096

void dev_init(const char* diskfile_path);
int dev_open(const char* diskfile_path);
void dev_close();
int bio_read(const int block_num, void *buf);
int bio_write(const int block_num, const void *buf);

#endif
